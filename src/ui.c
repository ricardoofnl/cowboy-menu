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
enum { UI_TOGGLE, UI_SUBMENU, UI_ACTION };

typedef struct {
    int          kind;
    const char  *label;
    int         *state;         // UI_TOGGLE
    void       (*page)(void);   // UI_SUBMENU
    void       (*fn)(void);     // UI_ACTION
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

// ---- draw -----------------------------------------------------------------
// normalized 0..1 coords; DRAW_RECT x/y is the rect center. all positions,
// sizes and colors below are eyeball starting points - tune on console.
static void ui_draw(void) {
    const float MX = 0.150f, MW = 0.200f;         // column center + width
    const float bannerY = 0.090f, bannerH = 0.050f;
    const float rowH = 0.035f;
    const float firstRowY = bannerY + bannerH * 0.5f + rowH * 0.5f + 0.004f;
    const float textL = MX - MW * 0.5f + 0.010f;  // left-aligned labels
    const float textR = MX + MW * 0.5f - 0.010f;  // right-aligned values

    // banner (western red) + title (cream)
    ui_draw_rect(MX, bannerY, MW, bannerH, 122, 20, 24, 235);
    ui_draw_text(s_banner ? s_banner : "cowboy menu",
                 MX, bannerY - 0.016f, 36, "title", "F2E9D0", "CENTER");

    if (s_itemCount <= 0) return;

    // dark body behind the rows
    float bodyH = s_itemCount * rowH;
    float bodyY = firstRowY - rowH * 0.5f + bodyH * 0.5f;
    ui_draw_rect(MX, bodyY, MW, bodyH, 18, 14, 10, 210);

    for (int i = 0; i < s_itemCount; i++) {
        float cy = firstRowY + i * rowH;
        int hot = (i == s_cursor);
        if (hot) ui_draw_rect(MX, cy, MW, rowH, 214, 158, 48, 235); // amber bar
        const char *hex = hot ? "1A1206" : "D8C9A8";
        float ty = cy - 0.012f;
        ui_draw_text(s_items[i].label, textL, ty, 22, "body", hex, "LEFT");
        if (s_items[i].kind == UI_TOGGLE) {
            const char *v = (s_items[i].state && *s_items[i].state) ? "On" : "Off";
            ui_draw_text(v, textR, ty, 22, "body", hex, "RIGHT");
        } else if (s_items[i].kind == UI_SUBMENU) {
            ui_draw_text(">", textR, ty, 22, "body", hex, "RIGHT");
        }
    }
}

// ---- per-frame tick --------------------------------------------------------
void ui_tick(void) {
    if (!s_open) return;

    // suppress the game reacting to our buttons this frame
    for (int i = 0; i < (int)(sizeof s_nav / sizeof s_nav[0]); i++) {
        ui_disable_ctrl(s_nav[i]);
        ui_input_excl(s_nav[i]);
    }

    // read nav as edges off the disabled controls (same as the base)
    int up     = ui_ctrl_just_dis(CTRL_UP);
    int down   = ui_ctrl_just_dis(CTRL_DOWN);
    int accept = ui_ctrl_just_dis(CTRL_ACCEPT);
    int cancel = ui_ctrl_just_dis(CTRL_CANCEL);

    // the opening frame's circle is still down - ignore it once
    if (s_justOpened) { s_justOpened = 0; up = down = accept = cancel = 0; }

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

    // select / toggle / push
    if (accept && s_itemCount > 0) {
        ui_item *it = &s_items[s_cursor];
        if (it->kind == UI_TOGGLE) {
            if (it->state) *it->state = !*it->state;
            ui_sound("SELECT");
        } else if (it->kind == UI_ACTION) {
            if (it->fn) it->fn();
            ui_sound("SELECT");
        } else if (it->kind == UI_SUBMENU) {
            if (it->page && s_sp < UI_MAX_DEPTH - 1) {
                s_savedCursor[s_sp] = s_cursor;
                s_stack[++s_sp] = it->page;
                s_cursor = 0;
                ui_sound("SELECT");
                return;         // child page builds + draws next frame
            }
        }
    }

    ui_draw();
}
