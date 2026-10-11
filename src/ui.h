#pragma once
#include <stdint.h>

// set by main.c before any ui call (defined in main.c)
extern uint64_t g_base;

// read R1+O while closed and open the menu (debounced to one shot)
void ui_toggle_open_with_r1_o(void);
int  ui_is_open(void);

// the root page build fn (shown when the menu first opens)
void ui_set_root(void (*page)(void));

// call once per frame while open: suppress game input, read nav,
// run the current page build fn, draw, process select/back/close
void ui_tick(void);

// immediate-mode items, called from inside a page build fn:
void ui_title(const char *label);                       // sets the banner text
void ui_toggle(const char *label, int *state);          // ACCEPT flips *state, draws a checkbox
void ui_submenu(const char *label, void (*page)(void)); // ACCEPT pushes page
void ui_action(const char *label, void (*fn)(void));    // ACCEPT calls fn (and shows a toast)
void ui_scroll_int(const char *label, int *val, int lo, int hi, int step); // LEFT/RIGHT adjust
void ui_scroll_opt(const char *label, int *idx, const char *const *opts, int count); // LEFT/RIGHT cycle

// show a toast for ~2.5s (actions call this for messages like "all weapons given")
void ui_notify(const char *msg);

