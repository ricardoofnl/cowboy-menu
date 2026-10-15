#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/features.h"

#define CONFIG_PATH "/tmp/cowboy_menu_test.ini"

uint64_t g_base;
FeatureState g_feat;
void ui_notify(const char *msg) { (void)msg; }

// scroller storage lives in features.c - define the same symbols for the host test
int g_cfg_money_idx, g_cfg_weather_idx, g_cfg_hour, g_cfg_minute;

#include "../src/config.c"

int main(void) {
    remove(CONFIG_PATH);

    // defaults: everything off -> load must not crash and keep defaults
    memset(&g_feat, 0, sizeof g_feat);
    g_cfg_money_idx = 2; g_cfg_weather_idx = 0; g_cfg_hour = 12; g_cfg_minute = 0;
    config_load();
    assert(!g_feat.god && !g_feat.unlimited_deadeye && g_cfg_hour == 12);

    // set some state, save, wipe, load -> roundtrip
    g_feat.god = 1; g_feat.unlimited_deadeye = 1; g_feat.horse_god = 1;
    g_feat.never_wanted = 1; g_feat.freeze_time = 1;
    g_cfg_money_idx = 5; g_cfg_weather_idx = 11; g_cfg_hour = 3; g_cfg_minute = 45;
    config_save();

    memset(&g_feat, 0, sizeof g_feat);
    g_cfg_money_idx = g_cfg_weather_idx = g_cfg_hour = g_cfg_minute = 0;
    config_load();

    assert(g_feat.god && g_feat.unlimited_deadeye && g_feat.horse_god);
    assert(g_feat.never_wanted && g_feat.freeze_time);
    assert(!g_feat.no_satchel && !g_feat.infinite_ammo);   // untouched stay off
    assert(g_cfg_money_idx == 5 && g_cfg_weather_idx == 11);
    assert(g_cfg_hour == 3 && g_cfg_minute == 45);

    // delete removes the file; a later load is a no-op
    config_delete();
    memset(&g_feat, 0, sizeof g_feat);
    config_load();
    assert(!g_feat.god);

    puts("config roundtrip tests passed");
    return 0;
}
