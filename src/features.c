#include <stdint.h>
#include <string.h>
#include "offsets.h"
#include "natives.h"
#include "ui.h"
#include "features.h"

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
  0x7BBD1FF6u, // revolver schofield
  0x0797FBF5u, // revolver double action
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
  0x1765A8F8u, // shotgun sawedoff
  0x63CA782Au, // shotgun repeating
  0x6D9BB970u, // shotgun semiauto
  0x88A8505Cu, // bow
  0xD2718D48u, // throwing knives
  0xA5E972D7u, // tomahawk
  0x28950C71u, // machete
  0xDB21AC8Cu, // knife
  0x7067E7A7u, // molotov
  0xA64DAA5Eu, // dynamite
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

// forward decls
static void player_page(void);
static void weapon_page(void);
static void horse_page(void);
static void give_all_weapons(void);
static void give_all_ammo(void);
static void give_all_weapons_to_horse(void);
static void equip_semi_auto(void);
static void equip_best_weapons(void);
static void dual_wield_enable(void);
static void dual_wield_disable(void);
static void take_all_weapons(void);
static void self_kill(void);
static void bonding_max(void);
static void horse_shop(void);

static int cur_player(void) { return (int)nat_call(g_base, OFF_PLAYER_ID, 0, 0); }
static int cur_ped(void) {
  int p = (int)nat_call(g_base, OFF_PLAYER_PED_ID, 0, 0);
  if (!p) p = (int)nat_call(g_base, OFF_GET_PLAYER_PED, (uint64_t[]){0xFF}, 1);
  return p;
}
// ---- menu layout ----

void features_build_root(void) {
  ui_title("Main");
  ui_submenu("Player", player_page);
  ui_submenu("Weapon Options", weapon_page);
  ui_submenu("Horse Options", horse_page);
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
  ui_action("Horse Shop", horse_shop);
}
// ---- per-frame apply ----

void features_apply_all(uint64_t base) {
  static FeatureState prev;   // last frame, for one-shot off/restore edges

  int player = (int)nat_call(base, OFF_PLAYER_ID, 0, 0);
  int ped    = (int)nat_call(base, OFF_PLAYER_PED_ID, 0, 0);
  if (!ped) ped = (int)nat_call(base, OFF_GET_PLAYER_PED, (uint64_t[]){0xFF}, 1);

  // god mode
  if (g_feat.god) {
    nat_call(base, OFF_SET_PLAYER_INVINCIBLE, (uint64_t[]){(uint32_t)player, 1}, 2);
    nat_call(base, OFF_SET_ENTITY_INVINCIBLE, (uint64_t[]){(uint32_t)ped, 1}, 2);
    nat_call(base, OFF_SET_ENTITY_CAN_BE_DAMAGED, (uint64_t[]){(uint32_t)ped, 0}, 2);
    nat_call(base, OFF_SET_ENTITY_PROOFS, (uint64_t[]){(uint32_t)ped, 0xFFFFFFFF, 1}, 3); // VERIFY proof arg form
  } else if (prev.god) {
    nat_call(base, OFF_SET_PLAYER_INVINCIBLE, (uint64_t[]){(uint32_t)player, 0}, 2);
    nat_call(base, OFF_SET_ENTITY_INVINCIBLE, (uint64_t[]){(uint32_t)ped, 0}, 2);
    nat_call(base, OFF_SET_ENTITY_CAN_BE_DAMAGED, (uint64_t[]){(uint32_t)ped, 1}, 2);
    nat_call(base, OFF_SET_ENTITY_PROOFS, (uint64_t[]){(uint32_t)ped, 0, 0}, 3);
  }

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

  // unlimited stamina
  if (g_feat.unlimited_stamina) {
    nat_call(base, OFF_RESTORE_PLAYER_STAMINA, (uint64_t[]){(uint32_t)player, fu(1.0f)}, 2);
    nat_call(base, OFF__SET_ATTRIBUTE_CORE_VALUE, (uint64_t[]){(uint32_t)ped, 1, 100}, 3); // VERIFY core 1=stamina
  }

  // unlimited deadeye - top up outer ring + pin core
  if (g_feat.unlimited_deadeye) {
    union { float f; uint32_t u; } mx;
    mx.u = (uint32_t)nat_call(base, OFF__GET_PLAYER_MAX_DEAD_EYE, (uint64_t[]){(uint32_t)player}, 1);
    nat_call(base, OFF__SPECIAL_ABILITY_RESTORE_OUTER_RING, (uint64_t[]){(uint32_t)player, (uint64_t)mx.u}, 2); // VERIFY args
    nat_call(base, OFF__SET_ATTRIBUTE_CORE_VALUE, (uint64_t[]){(uint32_t)ped, 2, 100}, 3); // VERIFY core 2=deadeye
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
  if (mount) {
    // unlimited stamina - the core write alone never touched the gallop-draining BAR; overpower
    // the stamina core (idx 1) so it stops draining.
    if (g_feat.horse_unlimited_stamina) {
      nat_call(base, OFF__SET_ATTRIBUTE_CORE_VALUE, (uint64_t[]){(uint32_t)mount, 1, 100}, 3);
      nat_call(base, OFF__ENABLE_ATTRIBUTE_CORE_OVERPOWER, (uint64_t[]){(uint32_t)mount, 1, fu(100.0f), 0}, 4);
      // the overpower core only refills the reservoir - the gallop drains the stamina BAR, so top
      // the bar itself every frame. that is what makes it truly unlimited (not just a one-shot tonic).
      union { float f; uint32_t u; } cur, mx;
      cur.u = (uint32_t)nat_call(base, OFF__GET_PED_STAMINA,     (uint64_t[]){(uint32_t)mount}, 1);
      mx.u  = (uint32_t)nat_call(base, OFF__GET_PED_MAX_STAMINA, (uint64_t[]){(uint32_t)mount}, 1);
      if (mx.f > cur.f) nat_call(base, OFF__CHANGE_PED_STAMINA, (uint64_t[]){(uint32_t)mount, fu(mx.f - cur.f)}, 2);
    } else if (prev.horse_unlimited_stamina) {
      nat_call(base, OFF__ENABLE_ATTRIBUTE_CORE_OVERPOWER, (uint64_t[]){(uint32_t)mount, 1, fu(0.0f), 0}, 4);
    }

    // god mode - block damage both ways, keep health core full, guard the bar restore
    if (g_feat.horse_god) {
      nat_call(base, OFF_SET_ENTITY_INVINCIBLE, (uint64_t[]){(uint32_t)mount, 1}, 2);
      nat_call(base, OFF_SET_ENTITY_CAN_BE_DAMAGED, (uint64_t[]){(uint32_t)mount, 0}, 2);
      nat_call(base, OFF__SET_ATTRIBUTE_CORE_VALUE, (uint64_t[]){(uint32_t)mount, 0, 100}, 3);
      int mh = (int)nat_call(base, OFF_GET_ENTITY_MAX_HEALTH, (uint64_t[]){(uint32_t)mount, 0}, 2);
      if (mh > 0) nat_call(base, OFF_SET_ENTITY_HEALTH, (uint64_t[]){(uint32_t)mount, (uint32_t)mh, 0}, 3);
    } else if (prev.horse_god) {
      nat_call(base, OFF_SET_ENTITY_INVINCIBLE, (uint64_t[]){(uint32_t)mount, 0}, 2);
      nat_call(base, OFF_SET_ENTITY_CAN_BE_DAMAGED, (uint64_t[]){(uint32_t)mount, 1}, 2);
    }

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

static void give_all_ammo(void) {
  int ped = cur_ped(); if (!ped) return;
  for (unsigned i = 0; i < NWEAPONS; i++) {
    uint32_t w = WEAPONS[i];
    nat_call(g_base, OFF__ENABLE_ALL_SPECIAL_AMMO_FOR_PED, (uint64_t[]){(uint32_t)ped, w}, 2);
    uint32_t at = (uint32_t)nat_call(g_base, OFF_GET_PED_AMMO_TYPE_FROM_WEAPON, (uint64_t[]){(uint32_t)ped, w}, 2);
    if (at) nat_call(g_base, OFF_SET_PED_AMMO_BY_TYPE, (uint64_t[]){(uint32_t)ped, at, 500}, 3);
    nat_call(g_base, OFF_SET_PED_AMMO, (uint64_t[]){(uint32_t)ped, w, 500}, 3);   // 3-arg, old n=4 was wrong
  }
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

static void self_kill(void) {
  int player = cur_player();
  int ped = cur_ped(); if (!ped) return;
  // drop god first or the damage won't register
  if (g_feat.god) {
    g_feat.god = 0;
    nat_call(g_base, OFF_SET_PLAYER_INVINCIBLE, (uint64_t[]){(uint32_t)player, 0}, 2);
    nat_call(g_base, OFF_SET_ENTITY_INVINCIBLE, (uint64_t[]){(uint32_t)ped, 0}, 2);
    nat_call(g_base, OFF_SET_ENTITY_CAN_BE_DAMAGED, (uint64_t[]){(uint32_t)ped, 1}, 2);
    nat_call(g_base, OFF_SET_ENTITY_PROOFS, (uint64_t[]){(uint32_t)ped, 0, 0}, 3);
  }
  nat_call(g_base, OFF_APPLY_DAMAGE_TO_PED, (uint64_t[]){(uint32_t)ped, 10000, 1, 0, (uint32_t)ped}, 5);
  nat_call(g_base, OFF_SET_ENTITY_HEALTH, (uint64_t[]){(uint32_t)ped, 0, 0}, 3);
}

static void bonding_max(void) {
  int ped = cur_ped(); if (!ped) return;
  if (!(int)nat_call(g_base, OFF_IS_PED_ON_MOUNT, (uint64_t[]){(uint32_t)ped}, 1)) return;
  int mount = (int)nat_call(g_base, OFF_GET_MOUNT, (uint64_t[]){(uint32_t)ped}, 1);
  if (mount) nat_call(g_base, OFF__SET_MOUNT_BONDING_LEVEL, (uint64_t[]){(uint32_t)mount, 4}, 2);
}

static void horse_shop(void) {
  // TODO need tack hashes - real saddle/tack joaat component hashes are not in the native db.
  // once sourced: resolve mount, then _APPLY_SHOP_ITEM_TO_PED(mount, tackHash, 1, 0, 0),
  // commit with _REFRESH_META_PED_SHOP_ITEMS + _UPDATE_PED_VARIATION + _SET_ACTIVE_META_PED_COMPONENTS_UPDATED.
}
