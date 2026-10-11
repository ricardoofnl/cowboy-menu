#include <GoldHEN/Common.h>   // orbis libkernel, goldhen (proc_info), detour api
#include "offsets.h"
#include "natives.h"

#ifndef attr_public
#define attr_public __attribute__((visibility("default")))
#endif

attr_public const char *g_pluginName    = "cowboy_menu";
attr_public const char *g_pluginDesc    = "rdr2 mod menu";
attr_public const char *g_pluginAuth    = "ricardoofnl";
attr_public uint32_t    g_pluginVersion = 0x00000100;

static uint64_t g_base = 0;

int32_t module_start(size_t argc, const void *args) {
    struct proc_info pi;
    if (sys_sdk_proc_info(&pi) != 0) return 0;
    if (strcmp(pi.titleid, GAME_TITLEID) != 0) return 0;   // only rdr2 eu
    if (!strstr(pi.version, GAME_VERSION)) return 0;        // offsets pinned to game 1.32
    if (pi.base_address == 0) return 0;

    g_base = pi.base_address;
    // next: hook the render/input tick, draw the menu (R1+O to open), run feature toggles
    (void)g_base;
    return 0;
}

int32_t module_stop(size_t argc, const void *args) {
    return 0;
}