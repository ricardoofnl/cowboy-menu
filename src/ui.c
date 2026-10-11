#include "ui.h"
#include "offsets.h"
#include "natives.h"
#include <stdio.h>
#include <stdint.h>

// ---- native arg packing helpers -------------------------------------------
#define UI_PTR(p) ((uint64_t)(uintptr_t)(p))

// floats ride in the low 32 bits of a slot (bit-cast, not converted)
static inline uint64_t ui_fbits(float f) {
    union { float f; uint32_t u; } u;
    u.f = f;
    return (uint64_t)u.u;
}

// ---- thin native wrappers -------------------------------------------------
static void ui_draw_rect(float x, float y, float w, float h,
                         int r, int g, int b, int a) {
    uint64_t s[10];
    s[0] = ui_fbits(x); s[1] = ui_fbits(y);
    s[2] = ui_fbits(w); s[3] = ui_fbits(h);
    s[4] = (uint32_t)r; s[5] = (uint32_t)g; s[6] = (uint32_t)b; s[7] = (uint32_t)a;
    s[8] = 0; s[9] = 1;   // p8=false, p9=true (copied from epinegui)
    nat_call(g_base, OFF_DRAW_RECT, s, 10);
}

static void ui_draw_text(const char *text, float x, float y, int size,
                         const char *face, const char *hex, const char *align) {
    char buf[512];
    snprintf(buf, sizeof buf,
        "<TEXTFORMAT INDENT='0' LEFTMARGIN='0' RIGHTMARGIN='0' LEADING='0'>"
        "<FONT FACE='$%s' COLOR='#%s' SIZE='%d'><P ALIGN='%s'>%s</P></FONT></TEXTFORMAT>",
        face, hex, size, align, text);

    // same two-step wrap the base uses: literal -> color string -> submit
    uint64_t a1[3] = { 10, UI_PTR("LITERAL_STRING"), UI_PTR(buf) };
    const char *lit = (const char *)(uintptr_t)nat_call(g_base, OFF_VAR_STRING, a1, 3);
    uint64_t a2[4] = { 42, UI_PTR("COLOR_STRING"), 0, UI_PTR(lit) };
    const char *col = (const char *)(uintptr_t)nat_call(g_base, OFF_VAR_STRING, a2, 4);

    uint64_t a3[3] = { UI_PTR(col), ui_fbits(x), ui_fbits(y) };
    nat_call(g_base, OFF__BG_DISPLAY_TEXT, a3, 3);
}

static int ui_ctrl_pressed(uint32_t c) {
    uint64_t s[2] = { 0, (uint64_t)c };
    return (int)nat_call(g_base, OFF_IS_CONTROL_PRESSED, s, 2);
}
static int ui_ctrl_just_dis(uint32_t c) {
    uint64_t s[2] = { 0, (uint64_t)c };
    return (int)nat_call(g_base, OFF_IS_DISABLED_CONTROL_JUST_PRESSED, s, 2);
}
static void ui_disable_ctrl(uint32_t c) {
    uint64_t s[3] = { 0, (uint64_t)c, 1 };
    nat_call(g_base, OFF_DISABLE_CONTROL_ACTION, s, 3);
}
static void ui_input_excl(uint32_t c) {
    uint64_t s[2] = { 0, (uint64_t)c };
    nat_call(g_base, OFF_SET_INPUT_EXCLUSIVE, s, 2);
}
static void ui_sound(const char *name) {
    uint64_t s[4] = { UI_PTR(name), UI_PTR("HUD_PLAYER_MENU"), 1, 0 };
    nat_call(g_base, OFF_PLAY_SOUND_FRONTEND, s, 4);
}

// ---- menu state -----------------------------------------------------------
enum { UI_TOGGLE, UI_SUBMENU, UI_ACTION, UI_SCROLL_INT, UI_SCROLL_OPT };

typedef struct {
    int                kind;
    const char        *label;
    int               *state;        // UI_TOGGLE flag / scroller value or index
    void             (*page)(void);  // UI_SUBMENU
    void             (*fn)(void);    // UI_ACTION
    int                lo, hi, step; // UI_SCROLL_INT bounds
    const char *const *opts;         // UI_SCROLL_OPT labels
    int                nopts;
} ui_item;

#define UI_MAX_ITEMS 32
#define UI_MAX_DEPTH 8

static ui_item     s_items[UI_MAX_ITEMS];
static int         s_itemCount;
static const char *s_banner;

static void      (*s_root)(void);
static void      (*s_stack[UI_MAX_DEPTH])(void);
static int         s_savedCursor[UI_MAX_DEPTH];
static int         s_sp;        // current depth, 0 = root
static int         s_cursor;

static int         s_open;
static int         s_comboPrev; // edge-detect for the R1+O combo
static int         s_justOpened;// swallow the opening frame's inputs

static float       s_hotY = -1.0f;  // smoothed highlight y, lerps toward the cursor row
static float       s_anim;          // 0..1 open-in progress (panel eases/slides in)
static char        s_noteMsg[128];  // toast text
static const char *s_noteCol = "E8D9B8";
static int         s_noteTimer;

// nav controls we suppress + read every frame while open
static const uint32_t s_nav[] = {
    CTRL_UP, CTRL_DOWN, CTRL_LEFT, CTRL_RIGHT, CTRL_ACCEPT, CTRL_CANCEL
};

// ---- public: open / query / root ------------------------------------------
int ui_is_open(void) { return s_open; }

void ui_set_root(void (*page)(void)) {
    s_root = page;
    s_stack[0] = page;
    s_sp = 0;
    s_cursor = 0;
}

void ui_toggle_open_with_r1_o(void) {
    int combo = ui_ctrl_pressed(CTRL_RB) && ui_ctrl_pressed(CTRL_CANCEL);
    int fresh = combo && !s_comboPrev;
    s_comboPrev = combo;
    if (!fresh) return;

    s_open = !s_open;
    if (s_open) {
        s_sp = 0;
        s_cursor = 0;
        s_stack[0] = s_root;
        s_justOpened = 1;       // eat the circle that was part of the combo
        s_anim = 0.0f;          // play the open-in animation
        s_hotY = -1.0f;         // snap the highlight to the first row
        s_noteTimer = 0;
        ui_sound("MENU_ENTER");
    } else {
        ui_sound("MENU_CLOSE");
    }
}

// ---- public: item registration (called from a page build fn) --------------
void ui_title(const char *label) { s_banner = label; }

static ui_item *ui_push(int kind, const char *label) {
    if (s_itemCount >= UI_MAX_ITEMS) return 0;
    ui_item *it = &s_items[s_itemCount++];
    it->kind = kind; it->label = label;
    it->state = 0; it->page = 0; it->fn = 0;
    it->lo = it->hi = it->step = 0; it->opts = 0; it->nopts = 0;
    return it;
}
void ui_toggle(const char *label, int *state) {
    ui_item *it = ui_push(UI_TOGGLE, label); if (it) it->state = state;
}
void ui_submenu(const char *label, void (*page)(void)) {
    ui_item *it = ui_push(UI_SUBMENU, label); if (it) it->page = page;
}
void ui_action(const char *label, void (*fn)(void)) {
    ui_item *it = ui_push(UI_ACTION, label); if (it) it->fn = fn;
}
void ui_scroll_int(const char *label, int *val, int lo, int hi, int step) {
    ui_item *it = ui_push(UI_SCROLL_INT, label);
    if (it) { it->state = val; it->lo = lo; it->hi = hi; it->step = step; }
}
void ui_scroll_opt(const char *label, int *idx, const char *const *opts, int count) {
    ui_item *it = ui_push(UI_SCROLL_OPT, label);
    if (it) { it->state = idx; it->opts = opts; it->nopts = count; }
}
void ui_notify(const char *msg) {
    snprintf(s_noteMsg, sizeof s_noteMsg, "%s", msg ? msg : "");
    s_noteCol = "E8D9B8";
    s_noteTimer = 150;
}

// ---- draw -----------------------------------------------------------------
// normalized 0..1 coords; DRAW_RECT x/y is the rect center. positions, sizes
// and colors are eyeball starting points - tune on console.

// toggle checkbox: a bordered box, filled amber (checked) when on, hollow when off
static void ui_checkbox(float cx, float cy, int on) {
    ui_draw_rect(cx, cy, 0.014f, 0.021f, 60, 48, 30, 240);   // frame
    ui_draw_rect(cx, cy, 0.010f, 0.015f, 18, 14, 10, 255);   // inner bg
    if (on) ui_draw_rect(cx, cy, 0.007f, 0.010f, 214, 158, 48, 255); // check mark
}

static void ui_draw(void) {
    const float MX = 0.825f, MW = 0.230f;         // top-right panel (epinegui layout)
    const float bannerY = 0.138f, bannerH = 0.040f;
    const float rowH = 0.035f;
    const float firstRowY = bannerY + bannerH * 0.5f + rowH * 0.5f + 0.004f;
    const float textL = MX - MW * 0.5f + 0.012f;
    const float textR = MX + MW * 0.5f - 0.016f;
    const float dx = (1.0f - s_anim) * 0.10f;     // slide in from the right
    // _BG_DISPLAY_TEXT ALIGN=CENTER centers in the field [x,1.0], so centered text at box
    // center CX needs x = 2*CX-1; vertically it sits ~size*0.0004 above the box center.
    const float cx = 2.0f * (MX + dx) - 1.0f;

    // brand box: COWBOY MENU centered inside the red box above the menu
    const float brandH = 0.046f;
    const float brandY = bannerY - bannerH * 0.5f - brandH * 0.5f - 0.003f;
    ui_draw_rect(MX + dx, brandY, MW, brandH, 122, 20, 24, 240);
    ui_draw_text("COWBOY MENU", cx, brandY - 30 * 0.0004f, 30, "title", "F2E9D0", "CENTER");

    // page box: current page name centered inside
    ui_draw_rect(MX + dx, bannerY, MW, bannerH, 60, 10, 12, 235);
    ui_draw_text(s_banner ? s_banner : "Menu", cx, bannerY - 22 * 0.0004f, 22, "body", "E8D9B8", "CENTER");

    if (s_itemCount <= 0) return;

    // dark body behind the rows
    float bodyH = s_itemCount * rowH;
    float bodyY = firstRowY - rowH * 0.5f + bodyH * 0.5f;
    ui_draw_rect(MX + dx, bodyY, MW, bodyH, 18, 14, 10, 210);

    // smooth highlight bar: ease toward the cursor row
    float targetY = firstRowY + s_cursor * rowH;
    if (s_hotY < 0.0f) s_hotY = targetY;
    else                s_hotY += (targetY - s_hotY) * 0.35f;
    ui_draw_rect(MX + dx, s_hotY, MW, rowH, 214, 158, 48, 235);

    for (int i = 0; i < s_itemCount; i++) {
        float cy = firstRowY + i * rowH;
        int hot = (i == s_cursor);
        const char *hex = hot ? "1A1206" : "D8C9A8";
        float ty = cy - 0.012f;
        ui_item *it = &s_items[i];
        // scrollers: draw the value INTO the label text (one left-aligned string) so it always
        // shows next to the label; X or left/right change it.
        if (it->kind == UI_SCROLL_INT) {
            char lb[96]; snprintf(lb, sizeof lb, "%s:  < %d >", it->label, it->state ? *it->state : 0);
            ui_draw_text(lb, textL + dx, ty, 22, "body", hex, "LEFT");
        } else if (it->kind == UI_SCROLL_OPT) {
            const char *v = (it->state && it->opts && *it->state >= 0 && *it->state < it->nopts)
                            ? it->opts[*it->state] : "";
            char lb[96]; snprintf(lb, sizeof lb, "%s:  < %s >", it->label, v);
            ui_draw_text(lb, textL + dx, ty, 22, "body", hex, "LEFT");
        } else {
            ui_draw_text(it->label, textL + dx, ty, 22, "body", hex, "LEFT");
            if (it->kind == UI_TOGGLE)
                ui_checkbox(textR + dx, cy, it->state && *it->state);
            else if (it->kind == UI_SUBMENU)
                ui_draw_text(">", textL + dx + 0.185f, ty, 22, "body", hex, "LEFT");
        }
    }

    // toast at the bottom-center (toggles + action messages via ui_notify)
    if (s_noteTimer > 0 && s_noteMsg[0]) {
        ui_draw_rect(0.5f, 0.900f, 0.30f, 0.040f, 18, 14, 10, 235);
        ui_draw_text(s_noteMsg, 0.0f, 0.900f - 20 * 0.0004f, 20, "body", s_noteCol, "CENTER");
    }
}

// ---- per-frame tick --------------------------------------------------------
void ui_tick(void) {
    if (!s_open) return;

    s_anim += (1.0f - s_anim) * 0.25f;    // ease the open-in animation
    if (s_noteTimer > 0) s_noteTimer--;   // age the toggle toast

    // suppress the game reacting to our buttons this frame
    for (int i = 0; i < (int)(sizeof s_nav / sizeof s_nav[0]); i++) {
        ui_disable_ctrl(s_nav[i]);
        ui_input_excl(s_nav[i]);
    }

    // read nav as edges off the disabled controls (same as the base)
    int up     = ui_ctrl_just_dis(CTRL_UP);
    int down   = ui_ctrl_just_dis(CTRL_DOWN);
    // left/right: edge-detect off the HELD disabled state - the "just pressed" variant doesn't
    // fire for frontend left/right on this build, so scrollers would never move.
    static int s_lHeld, s_rHeld;
    int lNow = (int)nat_call(g_base, OFF_IS_DISABLED_CONTROL_PRESSED, (uint64_t[]){0, CTRL_LEFT}, 2);
    int rNow = (int)nat_call(g_base, OFF_IS_DISABLED_CONTROL_PRESSED, (uint64_t[]){0, CTRL_RIGHT}, 2);
    int left  = lNow && !s_lHeld; s_lHeld = lNow;
    int right = rNow && !s_rHeld; s_rHeld = rNow;
    int accept = ui_ctrl_just_dis(CTRL_ACCEPT);
    int cancel = ui_ctrl_just_dis(CTRL_CANCEL);

    // the opening frame's circle is still down - ignore it once
    if (s_justOpened) { s_justOpened = 0; up = down = left = right = accept = cancel = 0; }

    // build the current page's item list
    s_itemCount = 0;
    s_banner = "cowboy menu";
    void (*page)(void) = s_stack[s_sp];
    if (page) page();

    // back / close
    if (cancel) {
        if (s_sp == 0) { s_open = 0; ui_sound("MENU_CLOSE"); return; }
        s_sp--;
        s_cursor = s_savedCursor[s_sp];
        s_hotY = -1.0f;
        ui_sound("BACK");
        return;                 // popped page draws next frame
    }

    // move cursor (wrap)
    if (s_itemCount > 0) {
        if (up)   { s_cursor = (s_cursor <= 0) ? s_itemCount - 1 : s_cursor - 1; ui_sound("NAV_UP"); }
        if (down) { s_cursor = (s_cursor >= s_itemCount - 1) ? 0 : s_cursor + 1; ui_sound("NAV_DOWN"); }
        if (s_cursor >= s_itemCount) s_cursor = s_itemCount - 1;
    } else {
        s_cursor = 0;
    }

    // left/right adjusts a scroller on the current row
    if ((left || right) && s_itemCount > 0) {
        ui_item *it = &s_items[s_cursor];
        if (it->kind == UI_SCROLL_INT && it->state) {
            *it->state += right ? it->step : -it->step;
            if (*it->state < it->lo) *it->state = it->lo;
            if (*it->state > it->hi) *it->state = it->hi;
            ui_sound("NAV_LEFT");
        } else if (it->kind == UI_SCROLL_OPT && it->state && it->nopts > 0) {
            *it->state = (*it->state + (right ? 1 : -1) + it->nopts) % it->nopts;
            ui_sound("NAV_LEFT");
        }
    }

    // select / toggle / push
    if (accept && s_itemCount > 0) {
        ui_item *it = &s_items[s_cursor];
        if (it->kind == UI_TOGGLE) {
            if (it->state) {
                *it->state = !*it->state;
                snprintf(s_noteMsg, sizeof s_noteMsg, "%s: %s", it->label, *it->state ? "ENABLED" : "DISABLED");
                s_noteCol = *it->state ? "7CCB6B" : "D8795A";
                s_noteTimer = 150;
            }
            ui_sound("SELECT");
        } else if (it->kind == UI_ACTION) {
            ui_notify(it->label);     // default toast; the fn can override via ui_notify()
            if (it->fn) it->fn();
            ui_sound("SELECT");
        } else if (it->kind == UI_SUBMENU) {
            if (it->page && s_sp < UI_MAX_DEPTH - 1) {
                s_savedCursor[s_sp] = s_cursor;
                s_stack[++s_sp] = it->page;
                s_cursor = 0;
                s_hotY = -1.0f;
                ui_sound("SELECT");
                return;         // child page builds + draws next frame
            }
        } else if (it->kind == UI_SCROLL_INT) {
            if (it->state) { *it->state += it->step; if (*it->state > it->hi) *it->state = it->lo; }
            ui_sound("SELECT");
        } else if (it->kind == UI_SCROLL_OPT) {
            if (it->state && it->nopts > 0) *it->state = (*it->state + 1) % it->nopts;
            ui_sound("SELECT");
        }
    }

    ui_draw();
}
