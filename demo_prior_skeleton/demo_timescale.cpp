#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "demo_playback.hpp"

#include "../console.hpp"
#include "../dvars.hpp"
#include "../scheduler.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/hook.hpp>

// =============================================================================
// MWR/H1 Demo Timescale Component
// Architecture mirrors IW6: https://git.alterware.dev/anomaly/iw6-mod
//   src/client/component/demo/demo_timescale.cpp  (no .hpp – IW6 has none)
//
// Registers the "demotimescale" dvar and applies it per-frame via a hook
// on CG_DrawActiveFrame so that playback speed is user-adjustable.
// =============================================================================

namespace demo_timescale
{
    namespace
    {
        // IW6 name: demotimescale dvar
        // Range: 0.1 – 10.0, default 1.0, flags DVAR_FLAG_NONE
        game::dvar_t* demotimescale_dvar = nullptr;

        // IW6 name: CG_DrawActiveFrame hook – apply timescale each frame
        // H1 CG_DrawActiveFrame: 0xF5D40 (MP) – already in symbols.hpp
        utils::hook::detour cg_draw_active_frame_hook;

        // IW6 name: demopause dvar (optional – mirrors IW6)
        game::dvar_t* demopause_dvar = nullptr;

        float get_timescale()
        {
            if (!demotimescale_dvar) return 1.0f;
            return demotimescale_dvar->current.value;
        }

        bool is_paused()
        {
            if (!demopause_dvar) return false;
            return demopause_dvar->current.enabled;
        }

        // IW6 name: CG_DrawActiveFrame hook callback
        // Signature matches H1 symbols.hpp:
        //   void(int localClientNum, int serverTime, int demoType,
        //        int cubemapShot, int cubemapSize, int renderScreen, unsigned int a7)
        void cg_draw_active_frame_stub(int local_client_num, int server_time,
                                       int demo_type, int cubemap_shot, int cubemap_size,
                                       int render_screen, unsigned int a7)
        {
            if (demo_playback::playing())
            {
                if (is_paused())
                {
                    // Skip frame advancement while paused
                    return;
                }

                const float ts = get_timescale();
                // TODO: track previous server_time and apply ts scaling.
                (void)ts;
            }

            cg_draw_active_frame_hook.invoke<void>(local_client_num, server_time,
                                                   demo_type, cubemap_shot, cubemap_size,
                                                   render_screen, a7);
        }

    } // anonymous namespace

    // =========================================================================
    // Component
    // =========================================================================

    class component final : public component_interface
    {
    public:
        void post_unpack() override
        {
            if (game::environment::is_dedi())
            {
                return;
            }

            // IW6 name: demotimescale
            scheduler::once([]()
            {
                demotimescale_dvar = dvars::register_float(
                    "demotimescale",
                    1.0f, 0.1f, 10.0f,
                    game::DVAR_FLAG_NONE,
                    "Demo playback speed multiplier");

                // IW6 name: demopause
                demopause_dvar = dvars::register_bool(
                    "demopause",
                    false,
                    game::DVAR_FLAG_NONE,
                    "Pause demo playback");
            }, scheduler::renderer);

            // IW6: CG_DrawActiveFrame hook (drives per-frame timescale)
            // H1 MP addr: 0xF5D40 (confirmed in h1-mod symbols.hpp)
            // The IW6 demo_playback.cpp hooks CG_VisionSetStartLerp_To at 0x1402A3200;
            // for H1 we hook CG_DrawActiveFrame which is already confirmed present.
            cg_draw_active_frame_hook.create(
                SELECT_VALUE(0x0_b, 0xF5D40_b), cg_draw_active_frame_stub);
        }
    };
}

REGISTER_COMPONENT(demo_timescale::component)
