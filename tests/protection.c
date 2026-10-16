#include <assert.h>
#include <stdio.h>
#include "../src/natives.h"

static uint64_t fake_nat_call(uint64_t base, uint64_t off, const uint64_t *args, uint32_t count);

// replace the invoker so lifecycle tests can run without the game
#define nat_call fake_nat_call
#include "../src/features.c"
#undef nat_call

uint64_t g_base;

typedef struct {
  int exists, invincible, damageable, stamina;
  uint32_t proofs;
} Entity;

static int deadeye_infinite[2];
static int deadeye_core[8];
static int deadeye_writes;

static Entity entities[8];
static int player_id, player_ped = 1, mounted_horse, saddle_horse;
static int player_god[2];
static unsigned mutations;
static int allow_kill;

static uint64_t fake_nat_call(uint64_t base, uint64_t off, const uint64_t *a, uint32_t n) {
  (void)base;
  switch (off) {
    case OFF_PLAYER_ID: return player_id;
    case OFF_PLAYER_PED_ID:
    case OFF_GET_PLAYER_PED: return player_ped;
    case OFF_IS_PED_ON_MOUNT: return mounted_horse != 0;
    case OFF_GET_MOUNT: return mounted_horse;
    case OFF__GET_SADDLE_HORSE_FOR_PLAYER: return saddle_horse;
    case OFF_DOES_ENTITY_EXIST:
      assert(n == 1 && a[0] < 8);
      return entities[a[0]].exists;
    case OFF_SET_PLAYER_INVINCIBLE:
      assert(n == 2 && a[0] < 2);
      player_god[a[0]] = a[1];
      mutations++;
      return 0;
    case OFF__MODIFY_INFINITE_TRAIL_VISION:
      assert(n == 2 && a[0] < 2);
      deadeye_infinite[a[0]] = a[1];
      deadeye_writes++;
      mutations++;
      return 0;
  }

  assert(n && a[0] > 0 && a[0] < 8);
  Entity *e = &entities[a[0]];
  assert(e->exists);
  mutations++;
  switch (off) {
    case OFF_SET_ENTITY_INVINCIBLE:
      assert(n == 2);
      e->invincible = a[1];
      break;
    case OFF_SET_ENTITY_CAN_BE_DAMAGED:
      assert(n == 2);
      e->damageable = a[1];
      break;
    case OFF_SET_ENTITY_PROOFS:
      assert(n == 3 && a[2] == (a[1] != 0));
      e->proofs = (uint32_t)a[1];
      break;
    case OFF_SET_PED_RESET_FLAG:
      assert(n == 3 && a[1] == 6);
      e->stamina = a[2];
      break;
    case OFF_APPLY_DAMAGE_TO_PED:
      assert(n == 5 && allow_kill);
      assert(!player_god[player_id] && !e->invincible && e->damageable && !e->proofs);
      break;
    case OFF_SET_ENTITY_HEALTH:
      assert(n == 3 && allow_kill && a[1] == 0);
      break;
    case OFF__SET_ATTRIBUTE_CORE_VALUE:
      assert(n == 3 && a[1] == 2);
      deadeye_core[a[0]] = a[2];
      break;
    default:
      fprintf(stderr, "unexpected native: 0x%llx\n", (unsigned long long)off);
      assert(0);
  }
  return 0;
}

static void tick(void) {
  for (unsigned i = 0; i < 8; i++) entities[i].stamina = 0;
  features_apply_all(g_base);
}

static void expect(int ped, int god, int stamina) {
  assert(entities[ped].invincible == god);
  assert(entities[ped].damageable == !god);
  assert(entities[ped].proofs == (god ? UINT32_MAX : 0));
  assert(entities[ped].stamina == stamina);
}

int main(void) {
  for (unsigned i = 1; i < 8; i++) entities[i] = (Entity){.exists = 1, .damageable = 1};
  tick();
  assert(!mutations);

  g_feat.god = g_feat.unlimited_stamina = 1;
  g_feat.horse_god = g_feat.horse_unlimited_stamina = 1;
  mounted_horse = saddle_horse = 2;
  for (int i = 0; i < 60; i++) {
    tick();
    expect(1, 1, 1);
    expect(2, 1, 1);
    assert(player_god[0]);
  }

  g_feat.god = g_feat.horse_unlimited_stamina = 0;
  tick();
  expect(1, 0, 1);
  expect(2, 1, 0);
  assert(!player_god[0]);
  g_feat.god = g_feat.horse_unlimited_stamina = 1;
  g_feat.unlimited_stamina = g_feat.horse_god = 0;
  tick();
  expect(1, 1, 0);
  expect(2, 0, 1);

  g_feat.unlimited_stamina = g_feat.horse_god = 1;
  mounted_horse = 3;
  tick();
  expect(2, 0, 0);
  expect(3, 1, 1);
  mounted_horse = 0;
  tick();
  expect(3, 1, 1);
  expect(2, 0, 0);

  g_feat.horse_god = g_feat.horse_unlimited_stamina = 0;
  tick();
  expect(3, 0, 0);
  g_feat.horse_god = g_feat.horse_unlimited_stamina = 1;
  tick();
  expect(2, 1, 1);

  entities[2].exists = 0;
  saddle_horse = 4;
  tick();
  expect(4, 1, 1);
  mounted_horse = 5;
  entities[5].exists = 0;
  tick();
  expect(4, 0, 0);
  mounted_horse = 0;
  tick();
  expect(4, 1, 1);

  player_ped = 6;
  tick();
  expect(1, 0, 0);
  expect(6, 1, 1);
  player_id = 1;
  tick();
  assert(!player_god[0] && player_god[1]);

  player_ped = 0;
  tick();
  expect(6, 0, 0);
  expect(4, 0, 0);
  assert(!player_god[1]);
  g_feat = (FeatureState){0};
  tick();
  player_ped = 7;
  tick();
  expect(7, 0, 0);

  g_feat.god = g_feat.unlimited_stamina = 1;
  tick();
  allow_kill = 1;
  self_kill();
  allow_kill = 0;
  expect(7, 0, 1);
  assert(!g_feat.god && !player_god[1]);
  tick();
  expect(7, 0, 1);

  g_feat = (FeatureState){0};
  tick();
  expect(7, 0, 0);

  // unlimited deadeye: edge-set the infinite flag, pin core each frame, release on off
  g_feat.unlimited_deadeye = 1;
  deadeye_writes = 0;
  tick();
  assert(deadeye_infinite[1] == 1 && deadeye_core[7] == 100 && deadeye_writes == 1);
  tick();
  assert(deadeye_writes == 1);   // flag set once on the edge, not re-written per frame
  g_feat.unlimited_deadeye = 0;
  tick();
  assert(deadeye_infinite[1] == 0 && deadeye_writes == 2);
  mutations = 0;
  tick();
  assert(!mutations);
  puts("protection lifecycle tests passed");
  return 0;
}

void ui_title(const char *label) { (void)label; }
void ui_toggle(const char *label, int *state) { (void)label; (void)state; }
void ui_submenu(const char *label, void (*page)(void)) { (void)label; (void)page; }
void ui_action(const char *label, void (*fn)(void)) { (void)label; (void)fn; }
void ui_scroll_int(const char *label, int *val, int lo, int hi, int step) {
  (void)label; (void)val; (void)lo; (void)hi; (void)step;
}
void ui_scroll_opt(const char *label, int *idx, const char *const *opts, int count) {
  (void)label; (void)idx; (void)opts; (void)count;
}
void ui_scroll_opt_apply(const char *label, int *idx, const char *const *opts, int count,
                         void (*apply)(int), int id) {
  (void)label; (void)idx; (void)opts; (void)count; (void)apply; (void)id;
}
void ui_notify(const char *msg) { (void)msg; }

// config stubs - persistence is file-based on console, no-op in tests
void config_save(void) {}
void config_delete(void) {}
void config_load(void) {}
