#include <stdint.h>
#include <string.h>
#include "offsets.h"
#include "natives.h"
#include "ui.h"
#include "features.h"
#include "config.h"

FeatureState g_feat = {0};

// pack a float into the low 32 bits of a native arg slot
static inline uint64_t fu(float f) { union { float f; uint32_t u; } x; x.f = f; return (uint64_t)x.u; }

// rage scrVector3: components sit in their own 8-byte slots (x@0, y@8, z@16). VERIFY layout on-console
typedef struct { float x; uint32_t _p0; float y; uint32_t _p1; float z; uint32_t _p2; } ScrVec3;

// firing-pattern joaat used by rapid fire. VERIFY
#define FIRING_PATTERN_FULL_AUTO 0xC6EE6B4Cu

// joaat("ADD_REASON_DEFAULT"). rdr2's inventory rejects an add with a zero reason, so
// GIVE_WEAPON_TO_PED silently does nothing without this - this was the core weapon bug.
#define ADD_REASON_DEFAULT 0x2CD419DCu

// canonical rdr2 weapon joaat hashes (build-independent, from femga/rdr3_discoveries weapons.lua)
static const uint32_t WEAPONS[] = {
  0x169F59F7u, // revolver cattleman
  0x16D655F7u, // revolver cattleman mexican
  0x7BBD1FF6u, // revolver schofield
  0x0797FBF5u, // revolver double action
  0x83DD5617u, // revolver double action gambler
  0x5B2D26B5u, // revolver lemat
  0x020D13FFu, // pistol volcanic
  0x657065D6u, // pistol semiauto
  0x8580C63Eu, // pistol mauser
  0x5B78B8DDu, // pistol m1899
  0xF5175BA1u, // repeater carbine
  0xA84762ECu, // repeater winchester
  0x95B24592u, // repeater henry
  0x7194721Eu, // repeater evans
  0x63F46DE6u, // rifle springfield
  0x772C8DD6u, // rifle boltaction
  0xDDF7BC1Eu, // rifle varmint
  0xE1D2B317u, // sniper rollingblock
  0x53944780u, // sniper carcano
  0x31B7B9FEu, // shotgun pump
  0x6DFA071Bu, // shotgun doublebarrel
  0x2250E150u, // shotgun doublebarrel exotic
  0x1765A8F8u, // shotgun sawedoff
  0x63CA782Au, // shotgun repeating
  0x6D9BB970u, // shotgun semiauto
  0x88A8505Cu, // bow
  0xD2718D48u, // throwing knives
  0xA5E972D7u, // tomahawk
  0x7F23B6C7u, // tomahawk ancient
  0x09E12A01u, // hatchet
  0x2A5CF9D6u, // hatchet hunter
  0xBCC63763u, // hatchet double bit
  0xEF32A25Du, // cleaver
  0x28950C71u, // machete
  0xDB21AC8Cu, // knife
  0x1086D041u, // knife jawbone
  0x67DC3FDEu, // torch
  0x7067E7A7u, // molotov
  0xA64DAA5Eu, // dynamite
  0x7A8A724Au, // lasso
};
#define NWEAPONS (sizeof(WEAPONS) / sizeof(WEAPONS[0]))

// no native classifies "semi-auto"; hardcode the three
static const uint32_t SEMIAUTO[] = { 0x657065D6u, 0x8580C63Eu, 0x6D9BB970u };
#define NSEMIAUTO (sizeof(SEMIAUTO) / sizeof(SEMIAUTO[0]))

// long guns only - the horse saddle holsters hold long arms + bow, not sidearms/throwables/melee
static const uint32_t LONGARMS[] = {
  0xF5175BA1u, 0xA84762ECu, 0x95B24592u, 0x7194721Eu, // repeaters
  0x63F46DE6u, 0x772C8DD6u, 0xDDF7BC1Eu,              // rifles
  0xE1D2B317u, 0x53944780u,                           // snipers
  0x31B7B9FEu, 0x6DFA071Bu, 0x63CA782Au, 0x6D9BB970u, // shotguns
  0x88A8505Cu,                                        // bow
};
#define NLONGARMS (sizeof(LONGARMS) / sizeof(LONGARMS[0]))

// money amounts in dollars (the game stores cents); scroller picks one
static const int MONEY_AMOUNTS[] = { 10, 50, 100, 500, 1000, 5000, 10000, 50000 };
static const char *const MONEY_LABELS[] = { "$10","$50","$100","$500","$1000","$5000","$10000","$50000" };
#define NMONEY (sizeof(MONEY_AMOUNTS) / sizeof(MONEY_AMOUNTS[0]))

// weather types (joaat of the lowercased name)
static const uint32_t WEATHER_HASHES[] = {
  0x614A1F91u,0x30FDAF5Cu,0xBB898D2Du,0x19D4F1D9u,0x995C7F44u,0x54A69840u,0xB677829Fu,
  0x7C1C4A13u,0x75A9E268u,0xEFB6EFF6u,0x23FB812Bu,0x27EA2814u,0xD61BDE01u,0x5974E8E5u,0xB17F6111u,
};
static const char *const WEATHER_NAMES[] = {
  "Sunny","Clouds","Overcast","Dark","Drizzle","Rain","Thunder",
  "Storm","Hail","Snow","Snow Light","Blizzard","Fog","Misty","Sandstorm",
};
#define NWEATHER (sizeof(WEATHER_HASHES) / sizeof(WEATHER_HASHES[0]))

// horse tack components (real joaat from rdr3 scripts)
static const uint32_t TACK[] = { 0xC10B5450u, 0x587DD49Fu, 0xC6C381F5u, 0x5277E9BAu, 0x508B80B9u, 0x9FD99D7Du };
#define NTACK (sizeof(TACK) / sizeof(TACK[0]))

// the wittemore (arthur's debauchery suit). shop-item component hashes from
// femga/rdr3_discoveries cloth_hash_names.lua - SP male. vest+tie pick honor variants.
static const uint32_t WITTEMORE_BLUE[] = {
  0xEB679BBFu, // coat  - CLOTHING_SP_COAT_HEIST01_VARIATION_01
  0x0E5E7EBCu, // pants - unnamed SP male
  0x8112D739u, // boots - CLOTHING_ITEM_BOOTS_PZERO_002
  0x5CB81FEBu, // vest  - CLOTHING_HL_PLAYER_VEST_010_1 (blue, high honor)
  0xDF4A0DD2u, // tie   - necktie (blue)
};
static const uint32_t WITTEMORE_RED[] = {
  0xEB679BBFu, 0x0E5E7EBCu, 0x8112D739u,
  0x8117E8AAu, // vest - CLOTHING_HL_PLAYER_VEST_010_4 (red, low honor)
  0x6743BFCBu, // tie  - CLOTHING_HL_PLAYER_NECKTIE_002_1 (red)
};
#define NWITTEMORE (sizeof(WITTEMORE_BLUE) / sizeof(WITTEMORE_BLUE[0]))

// scroller state for the money / weather / time menus - exposed for config save/load
int g_cfg_money_idx = 2, g_cfg_weather_idx = 0, g_cfg_hour = 12, g_cfg_minute = 0;
#define s_money_idx g_cfg_money_idx
#define s_weather_idx g_cfg_weather_idx
#define s_hour g_cfg_hour
#define s_minute g_cfg_minute

// forward decls
static void player_page(void);
static void weapon_page(void);
static void horse_page(void);
static void give_all_weapons(void);
static void max_upgrade_all_weapons(void);
static void give_all_ammo(void);
static void give_all_weapons_to_horse(void);
static void equip_semi_auto(void);
static void equip_best_weapons(void);
static void dual_wield_enable(void);
static void dual_wield_disable(void);
static void take_all_weapons(void);
static void self_kill(void);
static void clean_player(void);
static void outfit_page(void);
static void equip_wittemore_blue(void);
static void equip_wittemore_red(void);
static void bonding_max(void);
static void horse_shop(void);
static void money_page(void);
static void worldtime_page(void);
static void settings_page(void);
static void money_add(void);
static void money_remove(void);
static void apply_weather(void);
static void apply_time(void);

static int cur_player(void) { return (int)nat_call(g_base, OFF_PLAYER_ID, 0, 0); }
static int cur_ped(void) {
  int p = (int)nat_call(g_base, OFF_PLAYER_PED_ID, 0, 0);
  if (!p) p = (int)nat_call(g_base, OFF_GET_PLAYER_PED, (uint64_t[]){0xFF}, 1);
  return p;
}

typedef struct {
  int ped, god, stamina;
} PedProtection;

static PedProtection s_player_protection, s_horse_protection;
static int s_god_player = -1;
static int s_deadeye_player = -1;

static int entity_exists(uint64_t base, int entity) {
  return entity && (int)nat_call(base, OFF_DOES_ENTITY_EXIST, (uint64_t[]){(uint32_t)entity}, 1);
}

static void set_entity_god(uint64_t base, int ped, int on) {
  nat_call(base, OFF_SET_ENTITY_INVINCIBLE, (uint64_t[]){(uint32_t)ped, on}, 2);
  nat_call(base, OFF_SET_ENTITY_CAN_BE_DAMAGED, (uint64_t[]){(uint32_t)ped, !on}, 2);
  nat_call(base, OFF_SET_ENTITY_PROOFS, (uint64_t[]){(uint32_t)ped, on ? 0xFFFFFFFFu : 0, on}, 3);
}

static void apply_ped_protection(uint64_t base, PedProtection *state, int ped, int god, int stamina) {
  if (!entity_exists(base, ped)) ped = 0;
  if (state->ped != ped) {
    // undo persistent effects before moving to another entity
    if (entity_exists(base, state->ped)) {
      if (state->god) set_entity_god(base, state->ped, 0);
      if (state->stamina)
        nat_call(base, OFF_SET_PED_RESET_FLAG, (uint64_t[]){(uint32_t)state->ped, PRF_INFINITE_STAMINA, 0}, 3);
    }
    *state = (PedProtection){.ped = ped};
  }
  if (!ped) return;

  if (god || state->god) set_entity_god(base, ped, god);
  // reset flags expire each frame; this blocks stamina use without refilling it
  if (stamina || state->stamina)
    nat_call(base, OFF_SET_PED_RESET_FLAG, (uint64_t[]){(uint32_t)ped, PRF_INFINITE_STAMINA, stamina}, 3);
  state->god = god;
  state->stamina = stamina;
}

static void apply_player_protection(uint64_t base, int player, int ped) {
  if (!entity_exists(base, ped)) ped = 0;
  if (s_god_player >= 0 && (!g_feat.god || !ped || s_god_player != player)) {
    nat_call(base, OFF_SET_PLAYER_INVINCIBLE, (uint64_t[]){(uint32_t)s_god_player, 0}, 2);
    s_god_player = -1;
  }
  if (g_feat.god && ped) {
    nat_call(base, OFF_SET_PLAYER_INVINCIBLE, (uint64_t[]){(uint32_t)player, 1}, 2);
    s_god_player = player;
  }
  apply_ped_protection(base, &s_player_protection, ped, g_feat.god, g_feat.unlimited_stamina);
}

// ---- menu layout ----

void features_build_root(void) {
  ui_title("Main");
  ui_submenu("Player", player_page);
  ui_submenu("Weapon Options", weapon_page);
  ui_submenu("Horse Options", horse_page);
  ui_submenu("Money", money_page);
  ui_submenu("Weather & Time", worldtime_page);
  ui_submenu("Settings", settings_page);
}

static void settings_page(void) {
  ui_title("Settings");
  ui_action("Save Config", config_save);
  ui_action("Delete Config", config_delete);
}

static void player_page(void) {
  ui_title("Player");
  ui_toggle("God Mode", &g_feat.god);
  ui_toggle("Remove Satchel", &g_feat.no_satchel);
  ui_toggle("Never Wanted", &g_feat.never_wanted);
  ui_toggle("Super Run", &g_feat.super_run);
  ui_toggle("Super Swim", &g_feat.super_swim);
  ui_toggle("Super Jump", &g_feat.super_jump);
  ui_toggle("Invisibility", &g_feat.invisible);
  ui_toggle("Slow Motion", &g_feat.slow_motion);
  ui_toggle("Unlimited Stamina", &g_feat.unlimited_stamina);
  ui_toggle("Unlimited Deadeye", &g_feat.unlimited_deadeye);
  ui_toggle("Player Ignore", &g_feat.player_ignore);
  ui_submenu("Outfits", outfit_page);
  ui_action("Clean Player", clean_player);
  ui_action("Self Kill", self_kill);
}

static void weapon_page(void) {
  ui_title("Weapon Options");
  ui_action("Give All Weapons", give_all_weapons);
  ui_action("Give All Ammo", give_all_ammo);
  ui_action("Give All Weapons To Horse", give_all_weapons_to_horse);
  ui_action("Equip Semi-Auto", equip_semi_auto);
  ui_action("Equip Best Weapons", equip_best_weapons);
  ui_action("Dual Wield: Enable", dual_wield_enable);
  ui_action("Dual Wield: Disable", dual_wield_disable);
  ui_action("Take All Weapons", take_all_weapons);
  ui_action("Max Upgrade All Weapons", max_upgrade_all_weapons);
  ui_toggle("Infinite Ammo", &g_feat.infinite_ammo);
  ui_toggle("One Hit Kill", &g_feat.one_hit_kill);
  ui_toggle("Teleport Gun", &g_feat.teleport_gun);
  ui_toggle("Rapid Fire (exp)", &g_feat.rapid_fire);
  ui_toggle("Perfect Accuracy (exp)", &g_feat.perfect_accuracy);
  ui_toggle("Explosive Bullet (exp)", &g_feat.explosive_bullet);
}

static void horse_page(void) {
  ui_title("Horse Options");
  ui_toggle("Unlimited Stamina", &g_feat.horse_unlimited_stamina);
  ui_toggle("God Mode", &g_feat.horse_god);
  ui_toggle("Invisibility", &g_feat.horse_invisible);
  ui_toggle("Super Run", &g_feat.horse_super_run);
  ui_action("Bonding Level Max", bonding_max);
  ui_action("Upgrade Saddle & Tack", horse_shop);
}
// ---- per-frame apply ----

void features_apply_all(uint64_t base) {
  static FeatureState prev;   // last frame, for one-shot off/restore edges

  int player = (int)nat_call(base, OFF_PLAYER_ID, 0, 0);
  int ped    = (int)nat_call(base, OFF_PLAYER_PED_ID, 0, 0);
  if (!ped) ped = (int)nat_call(base, OFF_GET_PLAYER_PED, (uint64_t[]){0xFF}, 1);

  apply_player_protection(base, player, ped);

  // remove satchel - self-limiting strip chain, gated on the component check
  if (g_feat.no_satchel && ped &&
      (int)nat_call(base, OFF__IS_META_PED_USING_COMPONENT, (uint64_t[]){(uint32_t)ped, HASH_SATCHELS}, 2)) {
    nat_call(base, OFF_REMOVE_TAG_FROM_META_PED, (uint64_t[]){(uint32_t)ped, HASH_SATCHELS, 0}, 3);
    nat_call(base, OFF__UPDATE_PED_VARIATION, (uint64_t[]){(uint32_t)ped, 0, 0, 0, 0, 0}, 6);
    nat_call(base, OFF__SET_ACTIVE_META_PED_COMPONENTS_UPDATED, (uint64_t[]){(uint32_t)ped, 0}, 2);
  }

  // never wanted - set caps on enable, wipe score every frame, restore on disable
  if (g_feat.never_wanted) {
    if (!prev.never_wanted) {
      nat_call(base, OFF_SET_MAX_WANTED_LEVEL, (uint64_t[]){0}, 1);
      nat_call(base, OFF__SET_DISABLE_PLAYER_WANTED_LEVEL, (uint64_t[]){(uint32_t)player, 1}, 2);
      nat_call(base, OFF__SET_LAW_DISABLED, (uint64_t[]){1}, 1);
      nat_call(base, OFF_CLEAR_PLAYER_WANTED_LEVEL, (uint64_t[]){(uint32_t)player}, 1);
      nat_call(base, OFF_CLEAR_PLAYER_PAST_CRIMES, (uint64_t[]){(uint32_t)player}, 1);
    }
    nat_call(base, OFF_CLEAR_WANTED_SCORE, (uint64_t[]){(uint32_t)player}, 1);
  } else if (prev.never_wanted) {
    nat_call(base, OFF_SET_MAX_WANTED_LEVEL, (uint64_t[]){6}, 1); // VERIFY default max
    nat_call(base, OFF__SET_DISABLE_PLAYER_WANTED_LEVEL, (uint64_t[]){(uint32_t)player, 0}, 2);
    nat_call(base, OFF__SET_LAW_DISABLED, (uint64_t[]){0}, 1);
  }

  // super run - move_rate_override is animation-only (did nothing). a forward impulse each frame
  // while actually running is what speeds the player up. y=+20 is entity-local forward, flag 1 = impulse.
  if (g_feat.super_run && ped &&
      !(int)nat_call(base, OFF_IS_PED_RAGDOLL, (uint64_t[]){(uint32_t)ped}, 1) &&
      ((int)nat_call(base, OFF_IS_PED_RUNNING,   (uint64_t[]){(uint32_t)ped}, 1) ||
       (int)nat_call(base, OFF_IS_PED_SPRINTING, (uint64_t[]){(uint32_t)ped}, 1)))
    nat_call(base, OFF_APPLY_FORCE_TO_ENTITY,
      (uint64_t[]){(uint32_t)ped, 1, fu(0.0f), fu(20.0f), fu(0.0f), fu(0.0f), fu(0.0f), fu(0.0f),
                   1, 1, 1, 1, 0, 1}, 14);
  if (g_feat.super_swim) nat_call(base, OFF_SET_SWIM_MULTIPLIER_FOR_PLAYER, (uint64_t[]){(uint32_t)player, fu(1.49f)}, 2);
  if (g_feat.super_jump) nat_call(base, OFF_SET_SUPER_JUMP_THIS_FRAME, (uint64_t[]){(uint32_t)player}, 1); // weak in rdr2

  // invisibility
  if (g_feat.invisible)       nat_call(base, OFF_SET_ENTITY_VISIBLE, (uint64_t[]){(uint32_t)ped, 0}, 2);
  else if (prev.invisible)    nat_call(base, OFF_SET_ENTITY_VISIBLE, (uint64_t[]){(uint32_t)ped, 1}, 2);

  // slow motion
  if (g_feat.slow_motion)     nat_call(base, OFF_SET_TIME_SCALE, (uint64_t[]){fu(0.3f)}, 1);
  else if (prev.slow_motion)  nat_call(base, OFF_SET_TIME_SCALE, (uint64_t[]){fu(1.0f)}, 1);

  // unlimited deadeye - _MODIFY_INFINITE_TRAIL_VISION blocks drain on both deadeye and
  // eagle eye like the engine's own cheat does. persist flag is on the player so it only
  // needs an edge call; pin the core each frame so the gauge reads full.
  if (g_feat.unlimited_deadeye && ped) {
    if (s_deadeye_player >= 0 && s_deadeye_player != player)
      nat_call(base, OFF__MODIFY_INFINITE_TRAIL_VISION, (uint64_t[]){(uint32_t)s_deadeye_player, 0}, 2); // release old player
    if (s_deadeye_player != player) {
      nat_call(base, OFF__MODIFY_INFINITE_TRAIL_VISION, (uint64_t[]){(uint32_t)player, 1}, 2);
      s_deadeye_player = player;
    }
    nat_call(base, OFF__SET_ATTRIBUTE_CORE_VALUE, (uint64_t[]){(uint32_t)ped, 2, 100}, 3); // core 2 = deadeye
  } else if (s_deadeye_player >= 0) {
    nat_call(base, OFF__MODIFY_INFINITE_TRAIL_VISION, (uint64_t[]){(uint32_t)s_deadeye_player, 0}, 2);
    s_deadeye_player = -1;
  }

  // player ignore
  if (g_feat.player_ignore)     nat_call(base, OFF_SET_EVERYONE_IGNORE_PLAYER, (uint64_t[]){(uint32_t)player, 1}, 2);
  else if (prev.player_ignore)  nat_call(base, OFF_SET_EVERYONE_IGNORE_PLAYER, (uint64_t[]){(uint32_t)player, 0}, 2);
  // infinite ammo - clip native stops reloads; reserve ammo is PER-WEAPON (hash arg), so loop
  // over the weapon set. the old call passed weaponHash 0 which targets nothing, so reserve kept draining.
  if (g_feat.infinite_ammo && ped) {
    nat_call(base, OFF__SET_PED_INFINITE_AMMO_CLIP, (uint64_t[]){(uint32_t)ped, 1}, 2);
    for (unsigned i = 0; i < NWEAPONS; i++)
      nat_call(base, OFF_SET_PED_INFINITE_AMMO, (uint64_t[]){(uint32_t)ped, 1, WEAPONS[i]}, 3);
  } else if (prev.infinite_ammo && ped) {
    nat_call(base, OFF__SET_PED_INFINITE_AMMO_CLIP, (uint64_t[]){(uint32_t)ped, 0}, 2);
    for (unsigned i = 0; i < NWEAPONS; i++)
      nat_call(base, OFF_SET_PED_INFINITE_AMMO, (uint64_t[]){(uint32_t)ped, 0, WEAPONS[i]}, 3);
  }

  // one hit kill - damage modifier (simpler than looping instant-kill groups)
  if (g_feat.one_hit_kill)      nat_call(base, OFF_SET_PLAYER_WEAPON_DAMAGE_MODIFIER, (uint64_t[]){(uint32_t)player, fu(1000.0f)}, 2);
  else if (prev.one_hit_kill)   nat_call(base, OFF_SET_PLAYER_WEAPON_DAMAGE_MODIFIER, (uint64_t[]){(uint32_t)player, fu(1.0f)}, 2);

  // rapid fire - experimental, these likely only govern ai firing. VERIFY
  if (g_feat.rapid_fire) {
    nat_call(base, OFF_SET_PED_SHOOT_RATE, (uint64_t[]){(uint32_t)ped, 1000}, 2);
    nat_call(base, OFF_SET_PED_FIRING_PATTERN, (uint64_t[]){(uint32_t)ped, FIRING_PATTERN_FULL_AUTO}, 2);
  }

  // perfect accuracy - experimental, affects auto-aim/deadeye not manual aim. VERIFY
  if (g_feat.perfect_accuracy) {
    nat_call(base, OFF__SET_PLAYER_TOTAL_ACCURACY_MODIFIER, (uint64_t[]){(uint32_t)player, fu(100.0f)}, 2);
    nat_call(base, OFF_SET_PED_ACCURACY, (uint64_t[]){(uint32_t)ped, 100}, 2);
  } else if (prev.perfect_accuracy) {
    nat_call(base, OFF__SET_PLAYER_TOTAL_ACCURACY_MODIFIER, (uint64_t[]){(uint32_t)player, fu(1.0f)}, 2);
  }

  // explosive bullet - detonate at each NEW shot impact. the last-impact coord persists, so
  // dedupe or it re-blasts every frame at the same spot. type 25 = EXP_TAG_DYNAMITE (4 was molotov).
  {
    static float eb_lx, eb_ly, eb_lz; static int eb_primed;
    if (g_feat.explosive_bullet && ped) {
      if (!prev.explosive_bullet) eb_primed = 0;   // fresh enable: baseline off a stale impact
      ScrVec3 c; memset(&c, 0, sizeof c);
      int hit = (int)nat_call(base, OFF_GET_PED_LAST_WEAPON_IMPACT_COORD,
                              (uint64_t[]){(uint32_t)ped, (uint64_t)(uintptr_t)&c}, 2);
      if (hit && (!eb_primed || c.x != eb_lx || c.y != eb_ly || c.z != eb_lz)) {
        if (eb_primed)
          nat_call(base, OFF_ADD_OWNED_EXPLOSION,
            (uint64_t[]){(uint32_t)ped, fu(c.x), fu(c.y), fu(c.z), 25, fu(1.0f), 1, 0, fu(1.0f)}, 9);
        eb_lx = c.x; eb_ly = c.y; eb_lz = c.z; eb_primed = 1;
      }
    } else {
      eb_primed = 0;
    }
  }

  // teleport gun - jump to each NEW shot impact. the last-impact coord persists, so without a
  // dedupe it yanks you back every frame; +1z keeps you from clipping into the ground.
  {
    static float tg_lx, tg_ly, tg_lz; static int tg_primed;
    if (g_feat.teleport_gun && ped) {
      if (!prev.teleport_gun) tg_primed = 0;   // fresh enable: baseline, don't warp on a stale impact
      ScrVec3 c; memset(&c, 0, sizeof c);
      int hit = (int)nat_call(base, OFF_GET_PED_LAST_WEAPON_IMPACT_COORD,
                              (uint64_t[]){(uint32_t)ped, (uint64_t)(uintptr_t)&c}, 2);
      if (hit && (!tg_primed || c.x != tg_lx || c.y != tg_ly || c.z != tg_lz)) {
        if (tg_primed)
          nat_call(base, OFF_SET_ENTITY_COORDS,
            (uint64_t[]){(uint32_t)ped, fu(c.x), fu(c.y), fu(c.z + 1.0f), 0, 0, 0, 0}, 8);
        tg_lx = c.x; tg_ly = c.y; tg_lz = c.z; tg_primed = 1;
      }
    } else {
      tg_primed = 0;
    }
  }

  // ---- horse: resolve the mount, apply to it ----
  int anyHorse = g_feat.horse_unlimited_stamina || g_feat.horse_god ||
                 g_feat.horse_invisible || g_feat.horse_super_run ||
                 prev.horse_god || prev.horse_invisible || prev.horse_unlimited_stamina;
  int mount = 0;
  if (anyHorse && ped &&
      (int)nat_call(base, OFF_IS_PED_ON_MOUNT, (uint64_t[]){(uint32_t)ped}, 1)) {
    mount = (int)nat_call(base, OFF_GET_MOUNT, (uint64_t[]){(uint32_t)ped}, 1);
  }
  int protectedHorse = mount;
  if (!protectedHorse && ped && (g_feat.horse_god || g_feat.horse_unlimited_stamina)) {
    // keep the last horse protected after dismounting
    protectedHorse = s_horse_protection.ped;
    if (!entity_exists(base, protectedHorse))
      protectedHorse = (int)nat_call(base, OFF__GET_SADDLE_HORSE_FOR_PLAYER, (uint64_t[]){(uint32_t)player}, 1);
  }
  apply_ped_protection(base, &s_horse_protection, protectedHorse,
                       g_feat.horse_god, g_feat.horse_unlimited_stamina);

  if (mount) {
    // invisibility - SET_ENTITY_VISIBLE(false) cascades to the attached rider and hides the player;
    // alpha fades only the mount's own drawables, so the rider stays visible.
    if (g_feat.horse_invisible)       nat_call(base, OFF_SET_ENTITY_ALPHA, (uint64_t[]){(uint32_t)mount, 0, 0}, 3);
    else if (prev.horse_invisible)    nat_call(base, OFF_RESET_ENTITY_ALPHA, (uint64_t[]){(uint32_t)mount}, 1);

    // super run - impulse each frame while the horse is actually galloping (move-rate was anim-only)
    if (g_feat.horse_super_run &&
        !(int)nat_call(base, OFF_IS_PED_RAGDOLL, (uint64_t[]){(uint32_t)mount}, 1) &&
        ((int)nat_call(base, OFF_IS_PED_RUNNING,   (uint64_t[]){(uint32_t)mount}, 1) ||
         (int)nat_call(base, OFF_IS_PED_SPRINTING, (uint64_t[]){(uint32_t)mount}, 1)))
      nat_call(base, OFF_APPLY_FORCE_TO_ENTITY,
        (uint64_t[]){(uint32_t)mount, 1, fu(0.0f), fu(20.0f), fu(0.0f), fu(0.0f), fu(0.0f), fu(0.0f),
                     1, 1, 1, 1, 0, 1}, 14);
  }

  // freeze time - pause/unpause the in-game clock on the toggle edge
  if (g_feat.freeze_time && !prev.freeze_time) nat_call(base, OFF_PAUSE_CLOCK, (uint64_t[]){1, 0}, 2);
  else if (!g_feat.freeze_time && prev.freeze_time) nat_call(base, OFF_PAUSE_CLOCK, (uint64_t[]){0, 0}, 2);

  prev = g_feat;
}
// ---- one-shot actions ----

// one corrected GIVE_WEAPON_TO_PED call. inHand=1 forces it drawn, else holstered.
// args: ped, weapon, ammo, forceInHand, forceInHolster, attachPoint(0=primary),
//       allowMultipleCopies(0 so re-give does not stack), 0.5f, 1.0f, addReason(must be valid), ignoreUnlocks, degradation, p12
static void give_one_weapon(int ped, uint32_t w, int ammo, int inHand) {
  // 11th arg = bIgnoreUnlocks MUST be 1: with 0, story/unlock-locked weapons are silently
  // rejected (that was the partial give-all bug).
  nat_call(g_base, OFF_GIVE_WEAPON_TO_PED,
    (uint64_t[]){(uint32_t)ped, w, (uint32_t)ammo, inHand ? 1u : 0u, inHand ? 0u : 1u,
                 0, 0, fu(0.5f), fu(1.0f), ADD_REASON_DEFAULT, 1, fu(0.0f), 0}, 13);
  nat_call(g_base, OFF_SET_PED_AMMO, (uint64_t[]){(uint32_t)ped, w, (uint32_t)ammo}, 3);
}

static void give_all_weapons(void) {
  int ped = cur_ped(); if (!ped) return;
  for (unsigned i = 0; i < NWEAPONS; i++)
    give_one_weapon(ped, WEAPONS[i], 500, 0);   // holster each; they land in the wheel
  ui_notify("all weapons given");
}

// ---- max upgrade all weapons ----
// one functional "max" part per exclusive slot (longest barrel, extended clip/mag/tube, wide
// iron sight, grip/stock, scope on bolt rifles). cosmetic variants and FRAME_VERTDATA are left
// out (FRAME_VERTDATA breaks other parts' colors). component joaats from rdr3_discoveries.
static const uint32_t UC_CATTLEMAN[]   = { 0xDC341AAFu, 0x7FD24272u, 0x1B0D8E5Bu };
static const uint32_t UC_SCHOFIELD[]   = { 0xE4297827u, 0x3DC5FDB4u, 0x1AC7DA45u };
static const uint32_t UC_DBLACTION[]   = { 0xF3F3B915u, 0x12CCC827u, 0x07B98DFAu };
static const uint32_t UC_LEMAT[]       = { 0xDAF3F542u, 0xFFC04F8Cu, 0x1D3BC7CBu };
static const uint32_t UC_VOLCANIC[]    = { 0x060E9CC0u, 0x50751553u, 0x9F63E06Fu };
static const uint32_t UC_SEMIPISTOL[]  = { 0x1C1B73B5u, 0xAED91016u, 0x4996CF3Bu, 0x450D34A6u };
static const uint32_t UC_MAUSER[]      = { 0x23EEE15Du, 0xD530C2F5u, 0x1A81703Bu, 0xB668EC21u };
static const uint32_t UC_M1899[]       = { 0x6320A202u, 0xED5F4FD1u, 0x6E10FF97u, 0x1238DDA2u };
static const uint32_t UC_CARBINE[]     = { 0x8E4EEB68u, 0xBE847594u, 0x841D047Eu, 0xF0155D7Bu };
static const uint32_t UC_WINCHESTER[]  = { 0xC0039CB3u, 0x70E491BEu };
static const uint32_t UC_HENRY[]       = { 0x70659657u, 0xBA8F2CB4u };
static const uint32_t UC_EVANS[]       = { 0x992E47B8u, 0x19B9F165u };
static const uint32_t UC_SPRINGFIELD[] = { 0x65768D87u, 0x97DB6895u, 0x920B56D7u };
static const uint32_t UC_BOLTACTION[]  = { 0x4E58B5E0u, 0x4E2B941Cu, 0x920B56D7u };
static const uint32_t UC_VARMINT[]     = { 0xFCEE9E62u, 0x6F011007u, 0xB2B1CA8Du };
static const uint32_t UC_ROLLINGBLOCK[]= { 0x8D6E0798u, 0xDEE616DBu, 0x920B56D7u };
static const uint32_t UC_CARCANO[]     = { 0x570DC440u, 0xEE3095C6u, 0x82A9B7B5u };
static const uint32_t UC_SG_PUMP[]     = { 0xE8218EBCu, 0x16269609u, 0xADA238F1u, 0xF98B3538u };
static const uint32_t UC_SG_DOUBLE[]   = { 0xA1371765u, 0xA2909F48u, 0x575F58BEu, 0x2D6A1AFCu };
static const uint32_t UC_SG_SAWED[]    = { 0xF9AAA5A2u, 0xD2F83D2Du, 0xC69F8FC9u };
static const uint32_t UC_SG_REPEAT[]   = { 0x59D6C67Fu, 0x95688A19u, 0xE963C18Fu };
static const uint32_t UC_SG_SEMI[]     = { 0x79B1589Cu, 0xB57E782Du, 0x099EE88Fu };

typedef struct { uint32_t weapon; const uint32_t *comps; unsigned n; } WeaponUpgrade;
#define UG(w, arr) { (w), (arr), (unsigned)(sizeof(arr) / sizeof((arr)[0])) }
static const WeaponUpgrade WEAPON_UPGRADES[] = {
  UG(0x169F59F7u, UC_CATTLEMAN),   UG(0x7BBD1FF6u, UC_SCHOFIELD),
  UG(0x0797FBF5u, UC_DBLACTION),   UG(0x5B2D26B5u, UC_LEMAT),
  UG(0x020D13FFu, UC_VOLCANIC),    UG(0x657065D6u, UC_SEMIPISTOL),
  UG(0x8580C63Eu, UC_MAUSER),      UG(0x5B78B8DDu, UC_M1899),
  UG(0xF5175BA1u, UC_CARBINE),     UG(0xA84762ECu, UC_WINCHESTER),
  UG(0x95B24592u, UC_HENRY),       UG(0x7194721Eu, UC_EVANS),
  UG(0x63F46DE6u, UC_SPRINGFIELD), UG(0x772C8DD6u, UC_BOLTACTION),
  UG(0xDDF7BC1Eu, UC_VARMINT),     UG(0xE1D2B317u, UC_ROLLINGBLOCK),
  UG(0x53944780u, UC_CARCANO),     UG(0x31B7B9FEu, UC_SG_PUMP),
  UG(0x6DFA071Bu, UC_SG_DOUBLE),   UG(0x1765A8F8u, UC_SG_SAWED),
  UG(0x63CA782Au, UC_SG_REPEAT),   UG(0x6D9BB970u, UC_SG_SEMI),
};
#define NUPGRADES (sizeof(WEAPON_UPGRADES) / sizeof(WEAPON_UPGRADES[0]))

static void max_upgrade_all_weapons(void) {
  int ped = cur_ped(); if (!ped) return;
  for (unsigned i = 0; i < NUPGRADES; i++) {
    const WeaponUpgrade *u = &WEAPON_UPGRADES[i];
    give_one_weapon(ped, u->weapon, 500, 0);   // must own the gun before a component attaches
    // _GIVE_WEAPON_COMPONENT_TO_ENTITY(entity, componentHash, weaponHash, p3=1) - component THEN weapon
    for (unsigned j = 0; j < u->n; j++)
      nat_call(g_base, OFF__GIVE_WEAPON_COMPONENT_TO_ENTITY,
               (uint64_t[]){(uint32_t)ped, u->comps[j], u->weapon, 1}, 4);
  }
  ui_notify("all weapons maxed");
}

static void give_all_ammo(void) {
  int ped = cur_ped(); if (!ped) return;
  for (unsigned i = 0; i < NWEAPONS; i++) {
    uint32_t w = WEAPONS[i];
    // own the weapon first - SET_PED_AMMO / GET_PED_AMMO_TYPE_FROM_WEAPON are no-ops on an unowned
    // weapon (type resolves to 0), so the old refill never applied. give_one_weapon re-tops ammo.
    give_one_weapon(ped, w, 500, 0);
    nat_call(g_base, OFF__ENABLE_ALL_SPECIAL_AMMO_FOR_PED, (uint64_t[]){(uint32_t)ped, w}, 2);
    uint32_t at = (uint32_t)nat_call(g_base, OFF_GET_PED_AMMO_TYPE_FROM_WEAPON, (uint64_t[]){(uint32_t)ped, w}, 2);
    if (at) nat_call(g_base, OFF_SET_PED_AMMO_BY_TYPE, (uint64_t[]){(uint32_t)ped, at, 500}, 3); // shared pool
    nat_call(g_base, OFF_SET_PED_AMMO, (uint64_t[]){(uint32_t)ped, w, 500}, 3);                  // per-weapon
  }
  ui_notify("ammo refilled");
}

// give the whole arsenal, then show the long guns on the horse saddle holsters
static void give_all_weapons_to_horse(void) {
  int ped = cur_ped(); if (!ped) return;
  if (!(int)nat_call(g_base, OFF_IS_PED_ON_MOUNT, (uint64_t[]){(uint32_t)ped}, 1)) { ui_notify("mount a horse first"); return; }
  int mount = (int)nat_call(g_base, OFF_GET_MOUNT, (uint64_t[]){(uint32_t)ped}, 1);
  if (!mount) { ui_notify("mount a horse first"); return; }
  for (unsigned i = 0; i < NWEAPONS; i++) give_one_weapon(ped, WEAPONS[i], 500, 0);
  nat_call(g_base, OFF__DELETE_WEAPON_OBJECTS_ON_HORSE, (uint64_t[]){(uint32_t)mount}, 1); // clean rack first
  // attach each long arm: (horse, weaponHash, ownerPed). saddle only shows ~2 at once, rest stay owned
  for (unsigned i = 0; i < NLONGARMS; i++)
    nat_call(g_base, OFF__ATTACH_WEAPON_TO_HORSE_HOLSTER, (uint64_t[]){(uint32_t)mount, LONGARMS[i], (uint32_t)ped}, 3);
  ui_notify("weapons on the horse");
}

static void equip_semi_auto(void) {
  int ped = cur_ped(); if (!ped) return;
  give_one_weapon(ped, SEMIAUTO[0], 500, 1);   // primary drawn (forces stream+draw now)
  for (unsigned i = 1; i < NSEMIAUTO; i++) give_one_weapon(ped, SEMIAUTO[i], 500, 0);
  nat_call(g_base, OFF_SET_CURRENT_PED_WEAPON, (uint64_t[]){(uint32_t)ped, SEMIAUTO[0], 1, 0, 0, 0}, 6);
}

static void equip_best_weapons(void) {
  int ped = cur_ped(); if (!ped) return;
  int best = (int)nat_call(g_base, OFF_GET_BEST_PED_WEAPON, (uint64_t[]){(uint32_t)ped, 0, 0}, 3);
  if (best) nat_call(g_base, OFF_SET_CURRENT_PED_WEAPON, (uint64_t[]){(uint32_t)ped, (uint32_t)best, 1, 0, 0, 0}, 6);
}

static void dual_wield_enable(void)  { int ped = cur_ped(); if (ped) nat_call(g_base, OFF__SET_ALLOW_DUAL_WIELD, (uint64_t[]){(uint32_t)ped, 1}, 2); }
static void dual_wield_disable(void) { int ped = cur_ped(); if (ped) nat_call(g_base, OFF__SET_ALLOW_DUAL_WIELD, (uint64_t[]){(uint32_t)ped, 0}, 2); }
static void take_all_weapons(void)   { int ped = cur_ped(); if (ped) nat_call(g_base, OFF_REMOVE_ALL_PED_WEAPONS, (uint64_t[]){(uint32_t)ped, 1, 1}, 3); }

// wipes dirt, blood (body + face), wound decals and wetness off the player ped.
// same chain the game's own bathing script runs (bathing.c): env dirt + decal zones +
// blood damage + the _SET_PED_DIRT_CLEANED finisher which is what actually scrubs clothes.
static void clean_player(void) {
  int ped = cur_ped(); if (!ped) return;
  nat_call(g_base, OFF_CLEAR_PED_ENV_DIRT, (uint64_t[]){(uint32_t)ped}, 1);
  nat_call(g_base, OFF_CLEAR_PED_DAMAGE_DECAL_BY_ZONE,
           (uint64_t[]){(uint32_t)ped, 10, (uint64_t)(uintptr_t)"ALL"}, 3);
  nat_call(g_base, OFF_CLEAR_PED_BLOOD_DAMAGE, (uint64_t[]){(uint32_t)ped}, 1);
  nat_call(g_base, OFF__CLEAR_PED_BLOOD_DAMAGE_FACIAL, (uint64_t[]){(uint32_t)ped, 0}, 2);
  // per-zone scrub for the zones CLEAR_PED_BLOOD_DAMAGE can miss (face/hands/legs)
  for (uint32_t z = 0; z < 10; z++)
    nat_call(g_base, OFF_CLEAR_PED_BLOOD_DAMAGE_BY_ZONE, (uint64_t[]){(uint32_t)ped, z}, 2);
  nat_call(g_base, OFF_CLEAR_PED_WETNESS, (uint64_t[]){(uint32_t)ped}, 1);
  nat_call(g_base, OFF__SET_PED_DIRT_CLEANED, (uint64_t[]){(uint32_t)ped, fu(0.0f), (uint64_t)-1, 1, 1}, 5);
  nat_call(g_base, OFF__SET_PED_DAMAGE_CLEANLINESS, (uint64_t[]){(uint32_t)ped, 2}, 2);
  ui_notify("player cleaned");
}

// outfit page - wardrobe-style actions. each applies the shop-item components straight
// onto the player ped (same flow the horse tack uses), then refreshes the variation.
static void outfit_page(void) {
  ui_title("Outfits");
  ui_action("The Wittemore (High Honor)", equip_wittemore_blue);
  ui_action("The Wittemore (Low Honor)",  equip_wittemore_red);
}

static void equip_wittemore(const uint32_t *comps) {
  int ped = cur_ped(); if (!ped) return;
  nat_call(g_base, OFF__REFRESH_META_PED_SHOP_ITEMS, (uint64_t[]){(uint32_t)ped, 1}, 2);
  for (unsigned i = 0; i < NWITTEMORE; i++)
    nat_call(g_base, OFF__APPLY_SHOP_ITEM_TO_PED, (uint64_t[]){(uint32_t)ped, comps[i], 1, 0, 0}, 5);
  nat_call(g_base, OFF__UPDATE_PED_VARIATION, (uint64_t[]){(uint32_t)ped, 0, 1, 1, 1, 0}, 6);
  nat_call(g_base, OFF__SET_ACTIVE_META_PED_COMPONENTS_UPDATED, (uint64_t[]){(uint32_t)ped, 0}, 2);
  ui_notify("wittemore equipped");
}
static void equip_wittemore_blue(void) { equip_wittemore(WITTEMORE_BLUE); }
static void equip_wittemore_red(void)  { equip_wittemore(WITTEMORE_RED); }

static void self_kill(void) {
  int player = cur_player();
  int ped = cur_ped(); if (!ped) return;
  // drop god first or the damage won't register
  g_feat.god = 0;
  apply_player_protection(g_base, player, ped);
  nat_call(g_base, OFF_APPLY_DAMAGE_TO_PED, (uint64_t[]){(uint32_t)ped, 10000, 1, 0, (uint32_t)ped}, 5);
  nat_call(g_base, OFF_SET_ENTITY_HEALTH, (uint64_t[]){(uint32_t)ped, 0, 0}, 3);
}

static void bonding_max(void) {
  int ped = cur_ped(); if (!ped) return;
  int mount = 0;
  if ((int)nat_call(g_base, OFF_IS_PED_ON_MOUNT, (uint64_t[]){(uint32_t)ped}, 1))
    mount = (int)nat_call(g_base, OFF_GET_MOUNT, (uint64_t[]){(uint32_t)ped}, 1);
  if (!mount) mount = (int)nat_call(g_base, OFF__GET_SADDLE_HORSE_FOR_PLAYER, (uint64_t[]){(uint32_t)cur_player()}, 1);
  if (!mount) { ui_notify("no horse found"); return; }
  // the runtime bonding native alone snaps back; also write the backing attribute. PA_BONDING is
  // attribute index 7, range 0-4 (4 = max).
  nat_call(g_base, OFF_SET_ATTRIBUTE_BASE_RANK, (uint64_t[]){(uint32_t)mount, 7, 4}, 3);
  nat_call(g_base, OFF__SET_MOUNT_BONDING_LEVEL, (uint64_t[]){(uint32_t)mount, 4}, 2);
  ui_notify("bond maxed");
}

static void horse_shop(void) {
  int ped = cur_ped(); if (!ped) return;
  int mount = 0;
  if ((int)nat_call(g_base, OFF_IS_PED_ON_MOUNT, (uint64_t[]){(uint32_t)ped}, 1))
    mount = (int)nat_call(g_base, OFF_GET_MOUNT, (uint64_t[]){(uint32_t)ped}, 1);
  if (!mount) mount = (int)nat_call(g_base, OFF__GET_SADDLE_HORSE_FOR_PLAYER, (uint64_t[]){(uint32_t)cur_player()}, 1);
  if (!mount) { ui_notify("no horse found"); return; }
  nat_call(g_base, OFF__REFRESH_META_PED_SHOP_ITEMS, (uint64_t[]){(uint32_t)mount, 0}, 2);
  for (unsigned i = 0; i < NTACK; i++) {
    nat_call(g_base, OFF__APPLY_SHOP_ITEM_TO_PED, (uint64_t[]){(uint32_t)mount, TACK[i], 0, 0, 0}, 5); // sp
    nat_call(g_base, OFF__APPLY_SHOP_ITEM_TO_PED, (uint64_t[]){(uint32_t)mount, TACK[i], 0, 1, 0}, 5); // mp
  }
  nat_call(g_base, OFF__UPDATE_PED_VARIATION, (uint64_t[]){(uint32_t)mount, 0, 1, 1, 1, 0}, 6);
  nat_call(g_base, OFF__SET_ACTIVE_META_PED_COMPONENTS_UPDATED, (uint64_t[]){(uint32_t)mount, 0}, 2);
  ui_notify("tack applied");
}

// ---- money / weather / time ----
static void money_page(void) {
  ui_title("Money");
  ui_scroll_opt("Amount", &s_money_idx, MONEY_LABELS, (int)NMONEY);
  ui_action("Add Money", money_add);
  ui_action("Remove Money", money_remove);
}
static void money_add(void) {
  int cents = MONEY_AMOUNTS[s_money_idx] * 100;
  nat_call(g_base, OFF__MONEY_INCREMENT_CASH_BALANCE, (uint64_t[]){(uint32_t)cents, ADD_REASON_DEFAULT}, 2);
  ui_notify("money added");
}
static void money_remove(void) {
  int cents = MONEY_AMOUNTS[s_money_idx] * 100;
  int bal = (int)nat_call(g_base, OFF__MONEY_GET_CASH_BALANCE, 0, 0); // balance in cents
  if (cents > bal) cents = bal;                                       // clamp at 0
  if (cents > 0) nat_call(g_base, OFF__MONEY_DECREMENT_CASH_BALANCE, (uint64_t[]){(uint32_t)cents}, 1);
  ui_notify("money removed");
}

static void worldtime_page(void) {
  ui_title("Weather & Time");
  ui_scroll_opt("Weather", &s_weather_idx, WEATHER_NAMES, (int)NWEATHER);
  ui_action("Apply Weather", apply_weather);
  ui_scroll_int("Hour", &s_hour, 0, 23, 1);
  ui_scroll_int("Minute", &s_minute, 0, 59, 1);
  ui_action("Apply Time", apply_time);
  ui_toggle("Freeze Time", &g_feat.freeze_time);
}
static void apply_weather(void) {
  uint32_t w = WEATHER_HASHES[s_weather_idx];
  nat_call(g_base, OFF_SET_CURR_WEATHER_STATE, (uint64_t[]){w, w, fu(0.0f), 1}, 4); // instant snap
  nat_call(g_base, OFF_SET_WEATHER_TYPE, (uint64_t[]){w, 1, 1, 1, fu(0.0f), 0}, 6); // force it
  ui_notify("weather set");
}
static void apply_time(void) {
  nat_call(g_base, OFF_SET_CLOCK_TIME, (uint64_t[]){(uint32_t)s_hour, (uint32_t)s_minute, 0}, 3);
  ui_notify("time set");
}
