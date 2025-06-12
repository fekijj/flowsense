#include "engine_prediction.h"

#include "../features.h"
#include "future"
#include <thread>

void c_engine_prediction::on_render_start(int stage, bool after)
{
    if (stage != frame_render_start)
        return;

    if (!g_ctx.is_alive)
        return;

    if (!after)
    {
        interp_amount = interfaces::global_vars->interpolation_amount;

        if (g_exploits->recharge && !g_exploits->recharge_finish)
            interfaces::global_vars->interpolation_amount = 0.f;
    }
    else
        interfaces::global_vars->interpolation_amount = interp_amount;
}

void c_engine_prediction::net_compress_store_multithread(int tick)
{
    if (!g_ctx.local || !g_ctx.local->is_alive())
    {
        this->reset();
        return;
    }

    auto& data = net_data[tick % 150];

    if (!data.filled)
    {
        data.cmd_number = tick;
        data.vel_modifier = g_ctx.local->velocity_modifier();
        data.fall_velocity = g_ctx.local->fall_velocity();
        data.duck_amt = g_ctx.local->duck_amount();
        data.duck_speed = g_ctx.local->duck_speed();
        data.thirdperson_recoil = g_ctx.local->thirdperson_recoil();
        data.punch = g_ctx.local->aim_punch_angle();
        data.punch_vel = g_ctx.local->aim_punch_angle_vel();
        data.view_offset = g_ctx.local->view_offset();
        data.view_punch = g_ctx.local->view_punch_angle();
        data.velocity = g_ctx.local->velocity();
        data.filled = true;
    }

    reset_net_data = true;
}

void c_engine_prediction::net_compress_store(int tick)
{
    // Запускаем net_compress_store_multithread в нескольких потоках
    std::future< void > task1 = std::async(std::launch::async, &c_engine_prediction::net_compress_store_multithread, this, tick);
    std::future< void > task2 = std::async(std::launch::async, &c_engine_prediction::net_compress_store_multithread, this, tick);

    // Дожидаемся завершения задач
    task1.get();
    task2.get();
}

void c_engine_prediction::net_compress_apply_multithread(int tick) {
    if (!g_ctx.local || !g_ctx.local->is_alive()) {
        this->reset();
        return;
    }

    auto& data = net_data[tick % 150];
    if (data.cmd_number != tick || !data.filled)
        return;

    __m128 punch_diff = _mm_sub_ps(_mm_loadu_ps(&data.punch.x), _mm_loadu_ps(&g_ctx.local->aim_punch_angle().x));
    __m128 punch_vel_diff = _mm_sub_ps(_mm_loadu_ps(&data.punch_vel.x), _mm_loadu_ps(&g_ctx.local->aim_punch_angle_vel().x));
    __m128 view_offset_diff = _mm_sub_ps(_mm_loadu_ps(&data.view_offset.x), _mm_loadu_ps(&g_ctx.local->view_offset().x));

    const __m128 epsilon = _mm_set1_ps(0.03125f);

    __m128 punch_cmp = _mm_cmplt_ps(_mm_andnot_ps(_mm_set1_ps(-0.f), punch_diff), epsilon);
    if ((_mm_movemask_ps(punch_cmp) & 0b0111) == 0b0111)
        g_ctx.local->aim_punch_angle() = data.punch;

    __m128 punch_vel_cmp = _mm_cmplt_ps(_mm_andnot_ps(_mm_set1_ps(-0.f), punch_vel_diff), epsilon);
    if ((_mm_movemask_ps(punch_vel_cmp) & 0b0111) == 0b0111)
        g_ctx.local->aim_punch_angle_vel() = data.punch_vel;

    __m128 view_offset_cmp = _mm_cmplt_ps(_mm_andnot_ps(_mm_set1_ps(-0.f), view_offset_diff), epsilon);
    if ((_mm_movemask_ps(view_offset_cmp) & 0b0111) == 0b0111)
        g_ctx.local->view_offset() = data.view_offset;

    g_ctx.local->velocity_modifier() = data.vel_modifier;
    g_ctx.local->fall_velocity() = data.fall_velocity;
    g_ctx.local->duck_amount() = data.duck_amt;
    g_ctx.local->duck_speed() = data.duck_speed;
    g_ctx.local->thirdperson_recoil() = data.thirdperson_recoil;
    g_ctx.local->view_punch_angle() = data.view_punch;
    g_ctx.local->velocity() = data.velocity;

    reset_net_data = true;
}


void c_engine_prediction::net_compress_apply(int tick)
{
    // Запускаем net_compress_apply_multithread в нескольких потоках
    std::future< void > task1 = std::async(std::launch::async, &c_engine_prediction::net_compress_apply_multithread, this, tick);
    std::future< void > task2 = std::async(std::launch::async, &c_engine_prediction::net_compress_apply_multithread, this, tick);

    // Дожидаемся завершения задач
    task1.get();
    task2.get();
}

void c_engine_prediction::init() {
    if (!patterns::prediction_random_seed.as<int**>() || !patterns::prediction_player.as<int**>()) {
        return;
    }

    prediction_random_seed = *patterns::prediction_random_seed.as<int**>();
    prediction_player = *patterns::prediction_player.as<int**>();
}

void c_engine_prediction::update() {
    if (interfaces::client_state) {
        interfaces::prediction->update(
            interfaces::client_state->delta_tick, interfaces::client_state->delta_tick > 0, interfaces::client_state->last_command_ack, interfaces::client_state->last_outgoing_command + interfaces::client_state->choked_commands
        );
    }
}

void c_engine_prediction::start(c_csplayer* local, c_usercmd* cmd)
{
    if (!local || !cmd)
        return;

    auto weapon = local->get_active_weapon();
    if (!weapon)
        return;

    const int cmd_num = cmd->command_number;
    const float interval = interfaces::global_vars->interval_per_tick;
    const float cur_time = math::ticks_to_time(g_ctx.tick_base);
    const int seed = MD5_PseudoRandom(cmd_num) & 0x7FFFFFFF;

    unpred_vars[cmd_num % 150].fill();
    unprediced_velocity = local->velocity();
    unpredicted_flags = local->flags();

    old_cur_time = interfaces::global_vars->cur_time;
    old_frame_time = interfaces::global_vars->frame_time;
    old_tick_count = interfaces::global_vars->tick_count;
    old_in_prediction = interfaces::prediction->in_prediction;
    old_first_time_predicted = interfaces::prediction->is_first_time_predicted;
    old_seed = *prediction_random_seed;
    old_recoil_index = weapon->recoil_index();
    old_accuracy_penalty = weapon->accuracy_penalty();

    interfaces::global_vars->cur_time = cur_time;
    interfaces::global_vars->frame_time = interval;

    *(c_usercmd**)((uintptr_t)local + 0x3314) = cmd;
    *(c_usercmd*)((uintptr_t)local + 0x326C) = *cmd;

    interfaces::prediction->in_prediction = true;
    interfaces::prediction->is_first_time_predicted = false;

    *prediction_random_seed = seed;
    *prediction_player = reinterpret_cast<int>(local);

    this->repredict(local, cmd, true);
}


void c_engine_prediction::force_update_eyepos(const float& pitch) {
    auto local = g_ctx.local;
    if (!local)
        return;

    auto state = local->animstate();
    if (!state)
        return;

    const auto old_abs_angles = local->get_abs_angles();
    const auto old_poses = *(float*)((uintptr_t)local + offsets::m_flPoseParameter + 48);

    auto eye_pitch = pitch;

    if (eye_pitch > 180.f)
        eye_pitch -= 360.f;

    eye_pitch = std::clamp(eye_pitch, -90.f, 90.f);
    *(float*)((uintptr_t)local + offsets::m_flPoseParameter + 48) = std::clamp((eye_pitch + 90.f) / 180.f, 0.0f, 1.0f);

    local->invalidate_bone_cache();

    const auto old_abs_origin = local->get_abs_origin();
    local->set_abs_origin(local->origin());

    g_ctx.setup_bones[local->index()] = true;
    local->setup_bones(nullptr, -1, 0x100, local->simulation_time());
    g_ctx.setup_bones[local->index()] = false;

    local->set_abs_origin(old_abs_origin);

    g_ctx.eye_position = local->get_eye_position();

    *(float*)((uintptr_t)local + offsets::m_flPoseParameter + 48) = old_poses;
    local->set_abs_angles(old_abs_angles);
}


void c_engine_prediction::repredict(c_csplayer* local, c_usercmd* cmd, bool real_cmd) {
    if (!local || !cmd) return;

    auto* state = local->animstate();
    auto* weapon = local->get_active_weapon();
    if (!state || !weapon) return;

    const int old_tickbase = local->tickbase();

    if (real_cmd) {
        const auto impulse_ptr = reinterpret_cast<uint8_t*>((uintptr_t)local + 0x31EC);
        const uint8_t current_impulse = *impulse_ptr;

        g_ctx.cmd->buttons |= current_impulse;
        *impulse_ptr = g_ctx.cmd->impulse ? g_ctx.cmd->impulse : current_impulse;
    }

    interfaces::game_movement->start_track_prediction_errors(local);

    if (real_cmd) {
        const auto buttons_ptr = reinterpret_cast<int*>((uintptr_t)local + 0x31E8);
        const int previous_buttons = *buttons_ptr;
        const int current_buttons = g_ctx.cmd->buttons;

        const int buttons_changed = current_buttons ^ previous_buttons;

        *reinterpret_cast<int**>((uintptr_t)local + 0x31DC) = reinterpret_cast<int*>((uintptr_t)local + 0x31E8);
        *buttons_ptr = current_buttons;

        *reinterpret_cast<int*>((uintptr_t)local + 0x31E0) = current_buttons & buttons_changed;
        *reinterpret_cast<int*>((uintptr_t)local + 0x31E4) = buttons_changed & ~current_buttons;
    }

    // Обнуляем move_data (cache-aware memcpy быстрее memset на малых объемах, но оставим memset как проще)
    memset(&move_data, 0, sizeof(c_movedata));

    interfaces::prediction->check_moving_ground(local, interfaces::global_vars->frame_time);
    interfaces::prediction->set_local_view_angles(cmd->viewangles);

    local->run_pre_think();
    local->run_think();

    interfaces::move_helper->set_host(local);
    interfaces::prediction->setup_move(local, cmd, interfaces::move_helper, &move_data);
    interfaces::game_movement->process_movement(local, &move_data);
    interfaces::prediction->finish_move(local, cmd, &move_data);

    interfaces::move_helper->process_impacts();

    local->post_think();
    local->tickbase() = old_tickbase;

    interfaces::game_movement->finish_track_prediction_errors(local);
    interfaces::move_helper->set_host(nullptr);

    // Обновляем точность стрельбы, если нужно
    if (!weapon->is_misc_weapon()) {
        weapon->update_accuracy_penalty();
        predicted_inaccuracy = weapon->get_inaccuracy();
        predicted_spread = weapon->get_spread();
    }
}


void c_engine_prediction::finish_multithread(c_csplayer* local) {
    if (!local)
        return;

    c_basecombatweapon* weapon = local->get_active_weapon();
    if (weapon && !weapon->is_misc_weapon()) {
        // SIMD store для recoil и accuracy
        float* recoil_ptrs[] = { &weapon->recoil_index(), &weapon->accuracy_penalty() };
        const float vals[] = { old_recoil_index, old_accuracy_penalty };
        _mm_storeu_ps(recoil_ptrs[0], _mm_set_ps(0.f, 0.f, vals[1], vals[0])); // Только первые два значения важны
    }

    interfaces::global_vars->cur_time = old_cur_time;
    interfaces::global_vars->frame_time = old_frame_time;

    // Обнуляем cmd ptr
    *(c_usercmd**)((uintptr_t)local + 0x3314) = nullptr;

    *prediction_random_seed = old_seed;
    *prediction_player = 0;

    interfaces::prediction->in_prediction = old_in_prediction;
    interfaces::prediction->is_first_time_predicted = old_first_time_predicted;
}




void c_engine_prediction::finish(c_csplayer* local) {
    std::future<void> task = std::async(std::launch::async, &c_engine_prediction::finish_multithread, this, local);
    task.get();
}
