#include <GoldHEN/Common.h>   // orbis libkernel, goldhen (proc_info), detour api
#include "offsets.h"
#include "natives.h"
#include "ui.h"
#include "features.h"
#include "config.h"

#ifndef attr_public
#define attr_public __attribute__((visibility("default")))
#endif

attr_public const char *g_pluginName    = "cowboy_menu";
attr_public const char *g_pluginDesc    = "rdr2 mod menu";
attr_public const char *g_pluginAuth    = "ricardoofnl";
attr_public uint32_t    g_pluginVersion = 0x00000100;

uint64_t g_base = 0;   // running eboot base, referenced by ui.c and features.c

static Detour   g_detour;
static int      g_hooked = 0;
static unsigned g_lastFrame = 0;

typedef void (*get_player_ped_t)(NativeCtx *);

// per-frame tick on the script thread: the game calls GET_PLAYER_PED constantly, we gate it
// down to once per rendered frame with GET_FRAME_COUNT, then run input, menu and the toggles.
static void hk_tick(NativeCtx *ctx) {
    Detour_Stub(&g_detour, get_player_ped_t, ctx);   // let the original fill ctx->ret first

    unsigned f = (unsigned)nat_call(g_base, OFF_GET_FRAME_COUNT, 0, 0);
    if (f == g_lastFrame) return;
    g_lastFrame = f;

    ui_toggle_open_with_r1_o();
    if (ui_is_open()) ui_tick();
    features_apply_all(g_base);
}

int32_t module_start(size_t argc, const void *args) {
    struct proc_info pi;
    if (sys_sdk_proc_info(&pi) != 0) return 0;
    if (strcmp(pi.titleid, GAME_TITLEID) != 0) return 0;   // only rdr2 eu
    if (!strstr(pi.version, GAME_VERSION)) return 0;        // offsets pinned to game 1.32
    if (pi.base_address == 0) return 0;

    g_base = pi.base_address;
    ui_set_root(features_build_root);
    config_load();   // restore saved toggles before the first tick

    Detour_Construct(&g_detour, DetourMode_x64);
    Detour_DetourFunction(&g_detour, g_base + OFF_GET_PLAYER_PED, (void *)hk_tick);
    g_hooked = 1;
    return 0;
}

int32_t module_stop(size_t argc, const void *args) {
    if (g_hooked) {
        Detour_RestoreFunction(&g_detour);
        Detour_Destroy(&g_detour);
        g_hooked = 0;
    }
    return 0;
}