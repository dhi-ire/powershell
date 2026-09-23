#ifndef WM_H
#define WM_H

#include "types.h"
#include "gfx.h"
#include "input.h"

#define TITLE_H   28
#define BORDER    1
#define TASKBAR_H 40

enum { MOUSE_DOWN, MOUSE_UP, MOUSE_MOVE, MOUSE_DBLCLICK };

enum app_icon { ICON_TERMINAL, ICON_FILES, ICON_EDITOR, ICON_PAINT, ICON_CALC, ICON_ABOUT,
                ICON_FOLDER, ICON_FILE, ICON_REBOOT, ICON_POWER };

struct window {
    int x, y, w, h;               /* outer frame */
    struct surface *canvas;       /* client area */
    char title[64];
    int icon;
    bool minimized, dirty, closing;
    void *data;
    void (*on_paint)(struct window *);
    void (*on_key)(struct window *, struct key_event *);
    void (*on_mouse)(struct window *, int x, int y, int buttons, int event);
    void (*on_tick)(struct window *);            /* ~10 times a second */
    void (*on_close)(struct window *);
    struct window *next;          /* z-order: bottom -> top */
};

void wm_init(void);
void wm_run(void);
struct window *wm_create(const char *title, int icon, int cw, int ch);
void wm_close(struct window *w);
void wm_focus(struct window *w);
void wm_invalidate(struct window *w);
struct window *wm_windows(void);
struct window *wm_focused(void);
void wm_set_title(struct window *w, const char *title);

/* Shared widgets (theme-consistent). */
#define UI_BG      RGB(246, 247, 251)
#define UI_TEXT    RGB(33, 37, 48)
#define UI_MUTED   RGB(110, 118, 135)
#define UI_ACCENT  RGB(94, 129, 244)
#define UI_BORDER  RGB(208, 213, 224)

void ui_button(struct surface *s, int x, int y, int w, int h, const char *label, bool primary);
void ui_icon(struct surface *s, int x, int y, int icon, int size);

/* apps.c */
void app_launch(int icon, const char *arg);
void app_terminal(void);
void app_files(const char *path);
void app_editor(const char *path);
void app_paint(void);
void app_calc(void);
void app_about(void);

#endif
