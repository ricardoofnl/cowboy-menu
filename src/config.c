#include <stdio.h>
#include <string.h>
#include "features.h"
#include "ui.h"
#include "config.h"

// goldhen plugins can write anywhere under /data. keep it next to the loader config.
// overridable for host-side tests.
#ifndef CONFIG_PATH
#define CONFIG_PATH "/data/GoldHEN/cowboy_menu.ini"
#endif

// scroller defaults mirrored from features.c so a missing ini section restores them
extern int g_cfg_money_idx, g_cfg_weather_idx, g_cfg_hour, g_cfg_minute;

static const char *const FIELD[] = {
    "god", "no_satchel", "never_wanted", "super_run", "super_swim", "super_jump",
    "invisible", "slow_motion", "unlimited_stamina", "unlimited_deadeye", "player_ignore",
    "infinite_ammo", "one_hit_kill", "teleport_gun", "rapid_fire", "perfect_accuracy",
    "explosive_bullet",
    "horse_unlimited_stamina", "horse_god", "horse_invisible", "horse_super_run",
    "freeze_time",
};
#define NFIELDS (sizeof(FIELD) / sizeof(FIELD[0]))

static int *field_ptr(int i) {
    // FeatureState is a flat run of ints in declaration order - index into it
    // ponytail: a table of &g_feat.x would be safer against reordering; revisit if fields split
    _Static_assert(sizeof(FeatureState) / sizeof(int) >= 23, "field table out of sync");
    return &g_feat.god + i;
}

void config_load(void) {
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char key[64]; int v;
        if (sscanf(line, "%63[^=]=%d", key, &v) != 2) continue;
        for (unsigned i = 0; i < NFIELDS; i++) {
            if (!strcmp(key, FIELD[i])) { *field_ptr((int)i) = v != 0; goto next; }
        }
        if (!strcmp(key, "wittemore_variant")) g_feat.wittemore_variant = v;
        if (!strcmp(key, "money_idx"))    g_cfg_money_idx = v;
        if (!strcmp(key, "weather_idx"))  g_cfg_weather_idx = v;
        if (!strcmp(key, "hour"))         g_cfg_hour = v;
        if (!strcmp(key, "minute"))       g_cfg_minute = v;
    next:;
    }
    fclose(f);
}

void config_save(void) {
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) { ui_notify("save failed (no write access)"); return; }
    for (unsigned i = 0; i < NFIELDS; i++)
        fprintf(f, "%s=%d\n", FIELD[i], *field_ptr((int)i) ? 1 : 0);
    fprintf(f, "wittemore_variant=%d\nmoney_idx=%d\nweather_idx=%d\nhour=%d\nminute=%d\n",
            g_feat.wittemore_variant, g_cfg_money_idx, g_cfg_weather_idx,
            g_cfg_hour, g_cfg_minute);
    fclose(f);
    ui_notify("config saved");
}

void config_delete(void) {
    remove(CONFIG_PATH);
    ui_notify("config deleted");
}
