#include <assert.h>
#include <stdio.h>
#include "../src/natives.h"

static uint64_t fake_nat_call(uint64_t base, uint64_t off, const uint64_t *args, uint32_t count);

#define nat_call fake_nat_call
#include "../src/ui.c"
#include "../src/features.c"
#undef nat_call

uint64_t g_base;

static char drawn[64][768];
static unsigned drawn_count;
static uint32_t held, pressed;
static uint64_t action, action_args[16];

static uint64_t fake_nat_call(uint64_t base, uint64_t off, const uint64_t *a, uint32_t n) {
    (void)base;
    switch (off) {
        case OFF_VAR_STRING:
            return a[0] == 10 ? a[2] : a[3];
        case OFF__BG_DISPLAY_TEXT:
            assert(drawn_count < 64);
            snprintf(drawn[drawn_count++], sizeof drawn[0], "%s", (const char *)(uintptr_t)a[0]);
            return 0;
        case OFF_IS_DISABLED_CONTROL_PRESSED: return a[1] == held;
        case OFF_IS_DISABLED_CONTROL_JUST_PRESSED: return a[1] == pressed;
        case OFF_DRAW_RECT:
        case OFF_DISABLE_CONTROL_ACTION:
        case OFF_SET_INPUT_EXCLUSIVE:
        case OFF_PLAY_SOUND_FRONTEND:
            return 0;
        case OFF__MONEY_INCREMENT_CASH_BALANCE:
        case OFF_SET_CLOCK_TIME:
        case OFF_SET_WEATHER_TYPE:
        case OFF_SET_CURR_WEATHER_STATE:
            action = off;
            assert(n <= 16);
            memcpy(action_args, a, n * sizeof *a);
            return 0;
        default:
            fprintf(stderr, "unexpected native: 0x%llx\n", (unsigned long long)off);
            assert(0);
            return 0;
    }
}

static void frame(uint32_t hold, uint32_t press) {
    held = hold;
    pressed = press;
    drawn_count = 0;
    ui_tick();
}

static void tap(uint32_t control) {
    frame(0, 0);
    frame(control, control);
}

static void expect_text(const char *text) {
    char body[160];
    snprintf(body, sizeof body, ">%s</P>", text);
    for (unsigned i = 0; i < drawn_count; i++)
        if (strstr(drawn[i], body)) return;
    fprintf(stderr, "missing visible text: %s\n", text);
    assert(0);
}

int main(void) {
    ui_draw_text("Amount: < $100 > & time", 0.7f, 0.2f, 22, "body", "FFFFFF", "LEFT");
    expect_text("Amount: &lt; $100 &gt; &amp; time");

    ui_set_root(features_build_root);
    s_open = 1;
    s_anim = 1.0f;
    frame(0, 0);
    expect_text("&gt;");
    for (int i = 0; i < 3; i++) tap(CTRL_DOWN);
    tap(CTRL_ACCEPT);
    frame(0, 0);
    expect_text("Amount:  [ $100 ]");
    expect_text("D-pad Left / Right: change");
    expect_text("X: next value    O: back");

    tap(CTRL_LEFT);
    expect_text("Amount:  [ $50 ]");
    frame(CTRL_LEFT, 0);
    expect_text("Amount:  [ $50 ]");
    tap(CTRL_RIGHT);
    expect_text("Amount:  [ $100 ]");
    tap(CTRL_ACCEPT);
    expect_text("Amount:  [ $500 ]");
    tap(CTRL_DOWN);
    tap(CTRL_ACCEPT);
    assert(action == OFF__MONEY_INCREMENT_CASH_BALANCE && action_args[0] == 50000);
    expect_text("X: select    O: back");

    tap(CTRL_UP);
    for (int i = 0; i < 8; i++) {
        tap(CTRL_RIGHT);
        char expected[80];
        snprintf(expected, sizeof expected, "Amount:  [ %s ]", MONEY_LABELS[s_money_idx]);
        expect_text(expected);
    }
    assert(s_money_idx == 3);

    tap(CTRL_CANCEL);
    tap(CTRL_DOWN);
    tap(CTRL_ACCEPT);
    frame(0, 0);
    expect_text("Weather &amp; Time");
    expect_text("Weather:  [ Sunny ]");
    tap(CTRL_LEFT);
    expect_text("Weather:  [ Sandstorm ]");
    tap(CTRL_ACCEPT);
    expect_text("Weather:  [ Sunny ]");
    tap(CTRL_DOWN);
    tap(CTRL_ACCEPT);
    assert(action == OFF_SET_WEATHER_TYPE && action_args[0] == WEATHER_HASHES[0]);
    tap(CTRL_DOWN);
    expect_text("Hour:  [ 12 ]");
    tap(CTRL_RIGHT);
    expect_text("Hour:  [ 13 ]");
    for (int i = 0; i < 10; i++) tap(CTRL_RIGHT);
    expect_text("Hour:  [ 23 ]");
    tap(CTRL_RIGHT);
    expect_text("Hour:  [ 23 ]");
    tap(CTRL_ACCEPT);
    expect_text("Hour:  [ 0 ]");
    tap(CTRL_LEFT);
    expect_text("Hour:  [ 0 ]");
    tap(CTRL_DOWN);
    tap(CTRL_RIGHT);
    expect_text("Minute:  [ 1 ]");
    tap(CTRL_ACCEPT);
    expect_text("Minute:  [ 2 ]");
    tap(CTRL_DOWN);
    tap(CTRL_ACCEPT);
    assert(action == OFF_SET_CLOCK_TIME && action_args[0] == 0 && action_args[1] == 2);

    puts("ui selector regression tests passed");
    return 0;
}

// config stubs - persistence is file-based on console, no-op in tests
void config_save(void) {}
void config_delete(void) {}
void config_load(void) {}
