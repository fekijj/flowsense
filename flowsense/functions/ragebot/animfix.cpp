#include "animfix.h"
#include "../features.h"
#include "../../base/tools/threads.h"
#include <future>
#include <vector>

void c_animation_fix::anim_player_t::simulate_animation_side(records_t* record)
{
    auto state = ptr->animstate();
    if (!state) return;

    if (!last_record)
        state->last_update_time = (record->sim_time - interfaces::global_vars->interval_per_tick);

    restore_anims_t restore{};
    restore.store(ptr);

    if (!teammate)
        resolver::start(ptr, record);

    if (last_record && record->choke >= 2)
    {
        ptr->flags() = record->on_ground ? ptr->flags() | fl_onground : ptr->flags() & ~fl_onground;

        if (state->on_ground && state->velocity_length_xy <= 0.1f && !state->landing && state->last_update_increment > 0.f)
        {
            float delta = math::normalize(std::abs(state->abs_yaw - state->abs_yaw_last));
            if ((delta / state->last_update_increment) > 120.f)
            {
                record->sim_orig.layers[3].cycle = record->sim_orig.layers[3].weight = 0.f;
                record->sim_orig.layers[3].sequence = ptr->get_sequence_activity(979);
            }
        }
    }

    this->force_update();
    ptr->store_poses(record->poses);

    auto old = ptr->get_abs_origin();
    ptr->set_abs_origin(record->origin);

    record->sim_orig.layers[12].weight = last_record ? last_record->sim_orig.layers[12].weight : 0.f;

    ptr->set_layer(record->sim_orig.layers);
    this->build_bones(record, &record->sim_orig);
    ptr->set_abs_origin(old);

    restore.restore(ptr);
}

bool records_t::is_valid()
{
    auto netchan = interfaces::engine->get_net_channel_info();
    if (!interfaces::engine->get_net_channel_info() || !g_ctx.lagcomp || !valid) return false;

    float time = g_ctx.local->is_alive() ? g_ctx.predicted_curtime : interfaces::global_vars->cur_time;
    float correct = std::clamp(netchan->get_latency(flow_outgoing) + netchan->get_latency(flow_incoming) + g_ctx.lerp_time, 0.0f, cvars::sv_maxunlag->get_float());

    return std::fabsf(correct - (time - this->sim_time)) < 0.2f;
}

void c_animation_fix::anim_player_t::build_bones(records_t* record, records_t::simulated_data_t* sim)
{
    ptr->setup_uninterpolated_bones(sim->bone);
}

void c_animation_fix::anim_player_t::update_animations()
{
    backup_record.update_record(ptr);

    if (!records.empty())
    {
        last_record = &records.front();
        old_record = records.size() >= 3 ? &records[2] : nullptr;
    }

    auto& record = records.emplace_front();
    record.update_record(ptr);
    record.update_dormant(dormant_ticks);
    record.update_shot(last_record);

    dormant_ticks = dormant_ticks < 1 ? dormant_ticks + 1 : dormant_ticks;

    this->update_land(&record);
    this->update_velocity(&record);
    this->simulate_animation_side(&record);

    backup_record.restore(ptr);

    if (g_ctx.lagcomp && last_record)
    {
        if (last_record->sim_time > record.sim_time)
        {
            next_update_time = record.sim_time + std::abs(last_record->sim_time - record.sim_time) + math::ticks_to_time(1);
            record.valid = false;
        }
        else if (math::time_to_ticks(std::abs(next_update_time - record.sim_time)) > 17)
        {
            next_update_time = -1.f;
        }

        if (next_update_time > record.sim_time)
        {
            record.valid = false;
        }
    }

    const auto records_size = teammate ? 3 : g_ctx.tick_rate;
    if (records.size() > records_size)
        records.pop_back();
}

void thread_anim_update(c_animation_fix::anim_player_t* player)
{
    player->update_animations();
}

void c_animation_fix::on_net_update_and_render_after(int stage)
{
    if (!g_ctx.in_game || !g_ctx.local || g_ctx.uninject)
        return;

    const std::unique_lock<std::mutex> lock(mutexes::animfix);

    if (!g_ctx.in_game || !g_ctx.local || g_ctx.uninject)
        return;

    auto& players = g_listener_entity->get_entity(ent_player);
    if (players.empty())
        return;

    switch (stage)
    {
    case frame_net_update_postdataupdate_end:
    {
        g_rage_bot->on_pre_predict();

        std::vector<std::future<void>> futures;
        for (auto& player : players)
        {
            auto ptr = (c_csplayer*)player.m_entity;
            if (!ptr || ptr == g_ctx.local || !ptr->is_alive())
            {
                continue;
            }

            auto anim_player = this->get_animation_player(ptr->index());
            if (anim_player->ptr != ptr)
            {
                anim_player->reset_data();
                anim_player->ptr = ptr;
                continue;
            }

            futures.push_back(g_thread_pool->enqueue(thread_anim_update, anim_player));
        }

        for (auto& future : futures)
        {
            future.get();
        }
    }
    break;

    case frame_render_start:
    {
        for (auto& player : players)
        {
            auto entity = (c_csplayer*)player.m_entity;
            if (!entity || !entity->is_alive() || entity == g_ctx.local)
                continue;

            auto animation_player = this->get_animation_player(entity->index());
            if (!animation_player || animation_player->records.empty() || animation_player->dormant_ticks < 1)
            {
                g_ctx.setup_bones[entity->index()] = true;
                continue;
            }

            auto first_record = &animation_player->records.front();
            if (!first_record || !first_record->sim_orig.bone)
                continue;

            std::memcpy(first_record->render_bones, first_record->sim_orig.bone, sizeof(first_record->render_bones));
            math::change_matrix_position(first_record->render_bones, 128, first_record->origin, entity->get_render_origin());

            entity->interpolate_moveparent_pos();
            entity->set_bone_cache(first_record->render_bones);
            entity->attachments_helper();
        }
    }
    break;
    }
}