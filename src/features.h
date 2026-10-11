#pragma once
#include <stdint.h>

typedef struct {
  // player toggles
  int god, no_satchel, never_wanted, super_run, super_swim, super_jump,
      invisible, slow_motion, unlimited_stamina, unlimited_deadeye, player_ignore;
  // weapon toggles
  int infinite_ammo, one_hit_kill, rapid_fire, perfect_accuracy, explosive_bullet;
  // horse toggles
  int horse_unlimited_stamina, horse_god, horse_invisible, horse_super_run;
} FeatureState;

extern FeatureState g_feat;

void features_apply_all(uint64_t base);   // every frame from main.c tick
void features_build_root(void);           // ui_set_root(features_build_root)
