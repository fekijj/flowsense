#pragma once
#include "../../base/sdk.h"

#include "../../base/tools/math.h"

#include "../../base/other/game_functions.h"

#include "../../base/sdk/c_usercmd.h"
#include "../../base/sdk/entity.h"

#include <memory>
#include <optional>
#undef local

class c_engine_prediction
{
private:
    struct net_data_t
    {
        int cmd_number{ };

        float vel_modifier{ };
        float fall_velocity{ };
        float duck_amt{ };
        float duck_speed{ };
        float thirdperson_recoil{ };

        vector3d punch{ };
        vector3d punch_vel{ };
        vector3d view_offset{ };
        vector3d view_punch{ };
        vector3d velocity{ };

        bool filled{ };

        __forceinline void reset()
        {
            cmd_number = 0;

            vel_modifier = 0.f;
            fall_velocity = 0.f;
            duck_amt = 0.f;
            duck_speed = 0.f;
            thirdperson_recoil = 0.f;

            punch.reset();
            punch_vel.reset();
            view_offset.reset();
            view_punch.reset();
            velocity.reset();

            filled = false;
        }
    };

    struct netvars_t
    {
        bool done = false;

        float recoil_index;
        float acc_penalty;

        vector3d origin;
        vector3d abs_origin;
        vector3d viewoffset;
        vector3d aimpunch;
        vector3d aimpunch_vel;
        vector3d viewpunch;

        __forceinline void fill()
        {
            recoil_index = g_ctx.weapon->recoil_index();
            acc_penalty = g_ctx.weapon->accuracy_penalty();

            // origin
            _mm_storeu_ps(&origin.x, _mm_loadu_ps(&g_ctx.local->origin().x));

            // abs_origin
            _mm_storeu_ps(&abs_origin.x, _mm_loadu_ps(&g_ctx.local->get_abs_origin().x));

            // viewoffset
            _mm_storeu_ps(&viewoffset.x, _mm_loadu_ps(&g_ctx.local->view_offset().x));

            // aim_punch
            _mm_storeu_ps(&aimpunch.x, _mm_loadu_ps(&g_ctx.local->aim_punch_angle().x));
            _mm_storeu_ps(&aimpunch_vel.x, _mm_loadu_ps(&g_ctx.local->aim_punch_angle_vel().x));

            // viewpunch
            _mm_storeu_ps(&viewpunch.x, _mm_loadu_ps(&g_ctx.local->view_punch_angle().x));

            done = true;
        }

        __forceinline void set()
        {
            if (!done)
                return;

            g_ctx.weapon->recoil_index() = recoil_index;
            g_ctx.weapon->accuracy_penalty() = acc_penalty;

            // SIMD-копирование origin → origin
            _mm_storeu_ps(&g_ctx.local->origin().x, _mm_loadu_ps(&origin.x));

            // SIMD-копирование abs_origin → set_abs_origin
            {
                vector3d tmp;
                _mm_storeu_ps(&tmp.x, _mm_loadu_ps(&abs_origin.x));
                g_ctx.local->set_abs_origin(tmp);
            }

            // viewoffset
            _mm_storeu_ps(&g_ctx.local->view_offset().x, _mm_loadu_ps(&viewoffset.x));

            // aim_punch
            _mm_storeu_ps(&g_ctx.local->aim_punch_angle().x, _mm_loadu_ps(&aimpunch.x));
            _mm_storeu_ps(&g_ctx.local->aim_punch_angle_vel().x, _mm_loadu_ps(&aimpunch_vel.x));

            // viewpunch
            _mm_storeu_ps(&g_ctx.local->view_punch_angle().x, _mm_loadu_ps(&viewpunch.x));
        }




    };

    netvars_t unpred_vars[150];

    bool reset_net_data{ };
    bool old_in_prediction{ };
    bool old_first_time_predicted{ };

    int old_tick_base{ };
    int old_tick_count{ };

    float old_cur_time{ };
    float old_frame_time{ };

    float old_recoil_index{ };
    float old_accuracy_penalty{ };

    uint32_t old_seed{ };

    c_usercmd* old_cmd{ };

    int* prediction_player{ };
    int* prediction_random_seed{ };

    c_movedata move_data{ };

    std::array< net_data_t, 150 > net_data{ };

    __forceinline void reset()
    {
        if (!reset_net_data)
            return;

        for (auto& d : net_data)
            d.reset();

        reset_net_data = false;
    }

public:
    vector3d unprediced_velocity{ };
    int unpredicted_flags{ };
    int predicted_buttons{ };

    float predicted_inaccuracy{ };
    float predicted_spread{ };

    float interp_amount{ };

    void on_render_start(int stage, bool after);

    void net_compress_store_multithread(int tick);
    void net_compress_store(int tick);
    void net_compress_apply_multithread(int tick);
    void net_compress_apply(int tick);

    void init();
    void update();

    void start(c_csplayer* local, c_usercmd* cmd);
    void force_update_eyepos(const float& pitch);
    void repredict(c_csplayer* local, c_usercmd* cmd, bool real_cmd = false);
    void finish_multithread(c_csplayer* local);
    void finish(c_csplayer* local);
};