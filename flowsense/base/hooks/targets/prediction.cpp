#include "../hooks.h"
#include "../../../includes.h"

#include "../../sdk.h"
#include "../../global_context.h"

#include "../../../functions/config_vars.h"

#include "../../../base/sdk/entity.h"

#include "../../../functions/listeners/listener_entity.h"

#include "../../../functions/features.h"

namespace tr::prediction
{
    bool __fastcall in_prediction(void* ecx, void* edx)
    {
        static auto original = vtables[vtables_t::prediction].original<in_prediction_fn>(xor_int(14));

        // Early exit for simple conditions
        if (!g_ctx.in_game || !g_ctx.local || !g_ctx.local->is_alive())
            return original(ecx);

        // Check if recoil removal is enabled
        if (!(g_cfg.misc.removals & vis_recoil))
            return original(ecx);

        // Check for the specific return address
        if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == patterns::return_addr_drift_pitch.as<uintptr_t>())
            return true;

        return original(ecx);
    }

    void __fastcall run_command(void* ecx, void* edx, c_csplayer* player, c_usercmd* cmd, c_movehelper* move_helper)
    {
        static auto original = vtables[vtables_t::prediction].original<run_command_fn>(xor_int(19));

        // Early exit if not in game or if player is invalid
        if (!g_ctx.in_game || !g_ctx.local || !player->is_alive() || !g_ctx.weapon || player != g_ctx.local)
            return original(ecx, player, cmd, move_helper);

        // Call the original run_command
        original(ecx, player, cmd, move_helper);

        // Update viewmodel sequence
        g_utils->update_viewmodel_sequence(cmd, false);

        // Reset collision state
        player->collision_state() = 0;
    }

    void __fastcall process_movement(void* ecx, void* edx, c_csplayer* player, c_movedata* data)
    {
        static auto original = vtables[vtables_t::game_movement].original<process_movement_fn>(xor_int(1));

        // Fix prediction error in air by modifying movement variables
        data->game_code_moved_player = false;
        original(ecx, player, data);
    }
}
