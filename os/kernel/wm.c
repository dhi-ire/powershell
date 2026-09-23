/* Window manager, desktop, taskbar and start menu. */
#include "wm.h"
#include "cpu.h"
#include "font.h"
#include "heap.h"
#include "lib.h"
#include "rtc.h"
#include "io.h"

static struct window *windows;       /* bottom -> top; last is focused */
static struct surface *wallpaper;
static bool dirty = true;
static bool menu_open;
static int menu_hover = -1;

static struct mouse_state mouse;
static struct window *capture;        /* receives drag/move events */
static struct window *dragging;
static int drag_dx, drag_dy;
static int prev_buttons;

static int icon_selected = -1;
static uint32_t last_click_tick;
static int last_click_x, last_click_y;
static int last_minute = -1;

/* ---------- desktop / menu data ---------- */

struct desk_item { const char *label; int icon; };
static const struct desk_item desk_items[] = {
    { "Terminal", ICON_TERMINAL }, { "Files", ICON_FILES }, { "Text Editor", ICON_EDITOR },
    { "Paint", ICON_PAINT }, { "Calculator", ICON_CALC }, { "About", ICON_ABOUT },
};
#define NDESK (int)(sizeof(desk_items) / sizeof(desk_items[0]))

struct menu_item { const char *label; int icon; };
static const struct menu_item menu_items[] = {
    { "Terminal", ICON_TERMINAL }, { "Files", ICON_FILES }, { "Text Editor", ICON_EDITOR },
    { "Paint", ICON_PAINT }, { "Calculator", ICON_CALC }, { "About NovaOS", ICON_ABOUT },
    { NULL, 0 },
    { "Restart", ICON_REBOOT }, { "Shut down", ICON_POWER },
};
#define NMENU (int)(sizeof(menu_items) / sizeof(menu_items[0]))
#define MENU_W 250
#define MENU_ITEM_H 34
#define MENU_HDR_H 56

static int menu_h(void) { return MENU_HDR_H + (NMENU - 1) * MENU_ITEM_H + 10 + 8; }
static int menu_y(void) { return screen.h - TASKBAR_H - menu_h() - 6; }
#define MENU_X 6

static int menu_item_y(int i)
{
    int y = menu_y() + MENU_HDR_H;
    for (int k = 0; k < i; k++) y += menu_items[k].label ? MENU_ITEM_H : 10;
    return y;
}

static void desk_item_pos(int i, int *x, int *y) { *x = 24; *y = 24 + i * 92; }

/* ---------- icons ---------- */

/* Icons are drawn on a 16x16 grid scaled to `size`. */
void ui_icon(struct surface *s, int x, int y, int icon, int size)
{
    int u = size / 16;
    if (u < 1) u = 1;
#define R(gx, gy, gw, gh, c) gfx_fill(s, x + (gx) * u, y + (gy) * u, (gw) * u, (gh) * u, c)
    switch (icon) {
    case ICON_TERMINAL:
        gfx_round_rect(s, x, y + u, 16 * u, 14 * u, 2 * u, RGB(40, 44, 52));
        R(0, 1, 16, 2, RGB(70, 76, 90));
        R(2, 6, 1, 1, RGB(80, 250, 123)); R(3, 7, 1, 1, RGB(80, 250, 123));
        R(4, 8, 1, 1, RGB(80, 250, 123)); R(3, 9, 1, 1, RGB(80, 250, 123));
        R(2, 10, 1, 1, RGB(80, 250, 123)); R(6, 10, 5, 1, RGB(230, 230, 230));
        break;
    case ICON_FILES:
    case ICON_FOLDER:
        R(1, 3, 6, 2, RGB(230, 170, 40));
        gfx_round_rect(s, x + u, y + 4 * u, 14 * u, 10 * u, u, RGB(250, 196, 60));
        R(1, 6, 14, 1, RGB(255, 214, 100));
        break;
    case ICON_EDITOR:
    case ICON_FILE:
        R(3, 1, 10, 14, RGB(255, 255, 255));
        gfx_rect(s, x + 3 * u, y + u, 10 * u, 14 * u, RGB(150, 160, 180));
        R(5, 4, 6, 1, RGB(120, 130, 150)); R(5, 6, 6, 1, RGB(120, 130, 150));
        R(5, 8, 6, 1, RGB(120, 130, 150)); R(5, 10, 4, 1, RGB(120, 130, 150));
        if (icon == ICON_EDITOR) { R(9, 9, 2, 5, RGB(94, 129, 244)); R(9, 14, 2, 1, RGB(40, 44, 52)); }
        break;
    case ICON_PAINT:
        gfx_fill_circle(s, x + 8 * u, y + 8 * u, 7 * u, RGB(245, 230, 200));
        gfx_fill_circle(s, x + 5 * u, y + 6 * u, 2 * u, RGB(235, 80, 80));
        gfx_fill_circle(s, x + 9 * u, y + 4 * u, 2 * u, RGB(80, 170, 250));
        gfx_fill_circle(s, x + 12 * u, y + 8 * u, 2 * u, RGB(90, 200, 110));
        gfx_fill_circle(s, x + 7 * u, y + 11 * u, 2 * u, RGB(250, 200, 60));
        break;
    case ICON_CALC:
        gfx_round_rect(s, x + 2 * u, y, 12 * u, 16 * u, 2 * u, RGB(60, 66, 80));
        R(4, 2, 8, 3, RGB(170, 220, 170));
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                R(4 + c * 3, 7 + r * 3, 2, 2, c == 2 ? RGB(250, 160, 60) : RGB(220, 224, 232));
        break;
    case ICON_ABOUT:
        gfx_fill_circle(s, x + 8 * u, y + 8 * u, 7 * u, RGB(94, 129, 244));
        R(7, 4, 2, 2, 0xFFFFFF);
        R(7, 7, 2, 5, 0xFFFFFF);
        break;
    case ICON_REBOOT:
        gfx_fill_circle(s, x + 8 * u, y + 8 * u, 6 * u, RGB(80, 170, 250));
        gfx_fill_circle(s, x + 8 * u, y + 8 * u, 4 * u, UI_BG);
        R(8, 1, 4, 5, UI_BG);
        R(9, 2, 3, 3, RGB(80, 170, 250));
        break;
    case ICON_POWER:
        gfx_fill_circle(s, x + 8 * u, y + 8 * u, 6 * u, RGB(235, 80, 80));
        gfx_fill_circle(s, x + 8 * u, y + 8 * u, 4 * u, UI_BG);
        R(7, 1, 2, 3, UI_BG);
        R(7, 2, 2, 6, RGB(235, 80, 80));
        break;
    }
#undef R
}

void ui_button(struct surface *s, int x, int y, int w, int h, const char *label, bool primary)
{
    uint32_t bg = primary ? UI_ACCENT : RGB(232, 235, 242);
    gfx_round_rect(s, x, y, w, h, 4, primary ? RGB(70, 105, 220) : UI_BORDER);
    gfx_round_rect(s, x + 1, y + 1, w - 2, h - 2, 3, bg);
    int tw = gfx_text_width(label);
    gfx_text(s, x + (w - tw) / 2, y + (h - FONT_H) / 2, label, primary ? 0xFFFFFF : UI_TEXT);
}

/* ---------- window list ---------- */

struct window *wm_windows(void) { return windows; }

struct window *wm_focused(void)
{
    struct window *top = NULL;
    for (struct window *w = windows; w; w = w->next)
        if (!w->minimized && !w->closing) top = w;
    return top;
}

static void unlink_win(struct window *w)
{
    struct window **pp = &windows;
    while (*pp && *pp != w) pp = &(*pp)->next;
    if (*pp) *pp = w->next;
    w->next = NULL;
}

static void append_win(struct window *w)
{
    struct window **pp = &windows;
    while (*pp) pp = &(*pp)->next;
    *pp = w;
}

void wm_focus(struct window *w)
{
    unlink_win(w);
    append_win(w);
    w->minimized = false;
    dirty = true;
}

struct window *wm_create(const char *title, int icon, int cw, int ch)
{
    static int cascade;
    struct window *w = kcalloc(sizeof(*w));
    if (!w) return NULL;
    w->canvas = surface_new(cw, ch);
    if (!w->canvas) { kfree(w); return NULL; }
    gfx_fill(w->canvas, 0, 0, cw, ch, UI_BG);
    strlcpy(w->title, title, sizeof(w->title));
    w->icon = icon;
    w->w = cw + 2 * BORDER;
    w->h = ch + TITLE_H + BORDER;
    w->x = 150 + (cascade % 8) * 32;
    w->y = 50 + (cascade % 8) * 28;
    if (w->x + w->w > screen.w) w->x = screen.w - w->w;
    if (w->y + w->h > screen.h - TASKBAR_H) w->y = screen.h - TASKBAR_H - w->h;
    if (w->x < 0) w->x = 0;
    if (w->y < 0) w->y = 0;
    cascade++;
    w->dirty = true;
    append_win(w);
    menu_open = false;
    dirty = true;
    return w;
}

void wm_close(struct window *w) { w->closing = true; dirty = true; }

void wm_invalidate(struct window *w) { w->dirty = true; dirty = true; }

void wm_set_title(struct window *w, const char *title)
{
    strlcpy(w->title, title, sizeof(w->title));
    dirty = true;
}

static void reap_windows(void)
{
    struct window *w = windows;
    while (w) {
        struct window *next = w->next;
        if (w->closing) {
            if (w->on_close) w->on_close(w);
            if (capture == w) capture = NULL;
            if (dragging == w) dragging = NULL;
            unlink_win(w);
            surface_free(w->canvas);
            kfree(w);
            dirty = true;
        }
        w = next;
    }
}

/* ---------- drawing ---------- */

static void build_wallpaper(void)
{
    wallpaper = surface_new(screen.w, screen.h);
    struct surface *s = wallpaper;
    gfx_gradient(s, 0, 0, s->w, s->h, RGB(18, 24, 58), RGB(88, 40, 120));
    /* soft "aurora" bands */
    for (int x = 0; x < s->w; x++) {
        int t = x * 1024 / s->w;
        /* cheap wave from a triangle function */
        int tri = (t % 512) < 256 ? (t % 512) : 511 - (t % 512);
        int yc = s->h / 3 + (tri - 128) / 2;
        for (int k = -60; k < 60; k++) {
            int y = yc + k;
            if (y < 0 || y >= s->h) continue;
            int a = 60 - (k < 0 ? -k : k);
            uint32_t *p = &s->px[y * s->w + x];
            *p = gfx_blend(*p, RGB(60, 220, 180), a * 70 / 60);
        }
        int yc2 = s->h / 2 + (128 - tri) / 3;
        for (int k = -40; k < 40; k++) {
            int y = yc2 + k;
            if (y < 0 || y >= s->h) continue;
            int a = 40 - (k < 0 ? -k : k);
            uint32_t *p = &s->px[y * s->w + x];
            *p = gfx_blend(*p, RGB(120, 140, 255), a * 50 / 40);
        }
    }
    /* stars */
    uint32_t seed = 12345;
    for (int i = 0; i < 180; i++) {
        seed = seed * 1103515245 + 12345;
        int x = (seed >> 8) % s->w;
        seed = seed * 1103515245 + 12345;
        int y = (seed >> 8) % (s->h * 2 / 3);
        gfx_pixel(s, x, y, gfx_blend(s->px[y * s->w + x], 0xFFFFFF, 120 + (seed & 127)));
    }
    const char *brand = "NovaOS";
    int bx = s->w - 7 * 8 * 3 - 40, by = s->h - TASKBAR_H - 70;
    for (int i = 0; brand[i]; i++) {
        const unsigned char *g = font8x16[(int)brand[i]];
        for (int j = 0; j < 16; j++)
            for (int k = 0; k < 8; k++)
                if (g[j] & (0x80 >> k)) {
                    int px = bx + i * 26 + k * 3, py = by + j * 3;
                    for (int dy = 0; dy < 3; dy++)
                        for (int dx = 0; dx < 3; dx++)
                            if (px + dx < s->w && py + dy < s->h) {
                                uint32_t *p = &s->px[(py + dy) * s->w + px + dx];
                                *p = gfx_blend(*p, 0xFFFFFF, 60);
                            }
                }
    }
}

static void draw_desktop_icons(void)
{
    for (int i = 0; i < NDESK; i++) {
        int x, y;
        desk_item_pos(i, &x, &y);
        if (i == icon_selected)
            for (int j = 0; j < 84; j++)
                for (int k = 0; k < 88; k++) {
                    uint32_t *p = &screen.px[(y - 8 + j) * screen.w + x - 20 + k];
                    *p = gfx_blend(*p, RGB(120, 160, 255), 90);
                }
        ui_icon(&screen, x + 4, y, desk_items[i].icon, 48);
        const char *l = desk_items[i].label;
        int tx = x + 28 - gfx_text_width(l) / 2;
        gfx_text(&screen, tx + 1, y + 57, l, 0x000000);
        gfx_text(&screen, tx, y + 56, l, 0xFFFFFF);
    }
}

static void draw_window(struct window *w, bool focused)
{
    struct surface *s = &screen;
    gfx_shadow(s, w->x, w->y, w->w, w->h);
    uint32_t bar = focused ? RGB(38, 44, 60) : RGB(92, 98, 114);
    gfx_fill(s, w->x, w->y, w->w, w->h, bar);
    ui_icon(s, w->x + 8, w->y + 6, w->icon, 16);
    gfx_text(s, w->x + 32, w->y + 6, w->title, focused ? 0xFFFFFF : RGB(215, 218, 226));

    /* close + minimize buttons */
    int bx = w->x + w->w - 26, by = w->y + 6;
    gfx_round_rect(s, bx, by, 18, 16, 3, focused ? RGB(232, 72, 85) : RGB(130, 136, 150));
    gfx_line(s, bx + 5, by + 4, bx + 12, by + 11, 0xFFFFFF);
    gfx_line(s, bx + 12, by + 4, bx + 5, by + 11, 0xFFFFFF);
    gfx_line(s, bx + 6, by + 4, bx + 13, by + 11, 0xFFFFFF);
    gfx_line(s, bx + 13, by + 4, bx + 6, by + 11, 0xFFFFFF);
    bx -= 24;
    gfx_round_rect(s, bx, by, 18, 16, 3, focused ? RGB(70, 78, 98) : RGB(130, 136, 150));
    gfx_fill(s, bx + 5, by + 10, 8, 2, 0xFFFFFF);

    gfx_blit(s, w->canvas, w->x + BORDER, w->y + TITLE_H);
}

/* Taskbar window buttons shrink to fit between the start button and the clock. */
static int taskbar_btn_w(void)
{
    int n = 0;
    for (struct window *w = windows; w; w = w->next)
        if (!w->closing) n++;
    int avail = screen.w - 104 - 120;
    int bw = n ? avail / n - 6 : 170;
    if (bw > 170) bw = 170;
    if (bw < 40) bw = 40;
    return bw;
}

static void draw_taskbar(void)
{
    struct surface *s = &screen;
    int y = s->h - TASKBAR_H;
    for (int j = 0; j < TASKBAR_H; j++)
        for (int i = 0; i < s->w; i++) {
            uint32_t *p = &s->px[(y + j) * s->w + i];
            *p = gfx_blend(*p, RGB(16, 18, 28), 215);
        }
    gfx_hline(s, 0, y, s->w, RGB(70, 76, 100));

    /* start button */
    uint32_t sb = menu_open ? RGB(94, 129, 244) : RGB(52, 58, 80);
    gfx_round_rect(s, 6, y + 5, 86, 30, 6, sb);
    gfx_fill_circle(s, 22, y + 20, 7, RGB(120, 220, 200));
    gfx_fill_circle(s, 22, y + 20, 3, RGB(18, 24, 58));
    gfx_text_bold(s, 36, y + 12, "Nova", 0xFFFFFF);

    /* window buttons */
    int x = 104, bw = taskbar_btn_w();
    struct window *focus = wm_focused();
    for (struct window *w = windows; w; w = w->next) {
        if (w->closing) continue;
        uint32_t bg = (w == focus) ? RGB(70, 80, 120) : RGB(40, 44, 62);
        gfx_round_rect(s, x, y + 5, bw, 30, 5, bg);
        if (w == focus) gfx_fill(s, x + bw / 2 - 25, y + 32, 50, 2, RGB(120, 160, 255));
        ui_icon(s, x + 8, y + 12, w->icon, 16);
        int chars = (bw - 36) / FONT_W;
        if (chars > 0) {
            char t[24];
            strlcpy(t, w->title, chars + 1 < (int)sizeof(t) ? chars + 1 : (int)sizeof(t));
            gfx_text(s, x + 30, y + 12, t, w->minimized ? RGB(150, 155, 170) : 0xFFFFFF);
        }
        x += bw + 6;
    }

    /* clock */
    struct datetime dt;
    rtc_read(&dt);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", dt.hour, dt.minute);
    gfx_text_bold(s, s->w - 16 - gfx_text_width(buf), y + 4, buf, 0xFFFFFF);
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", dt.year, dt.month, dt.day);
    gfx_text(s, s->w - 12 - gfx_text_width(buf), y + 20, buf, RGB(180, 186, 200));
    last_minute = dt.minute;
}

static void draw_menu(void)
{
    struct surface *s = &screen;
    int x = MENU_X, y = menu_y(), h = menu_h();
    gfx_shadow(s, x, y, MENU_W, h);
    gfx_round_rect(s, x, y, MENU_W, h, 8, UI_BG);
    gfx_round_rect(s, x, y, MENU_W, MENU_HDR_H - 6, 8, RGB(38, 44, 60));
    gfx_fill(s, x, y + MENU_HDR_H - 14, MENU_W, 8, RGB(38, 44, 60));
    gfx_fill_circle(s, x + 26, y + 25, 15, RGB(94, 129, 244));
    gfx_text_bold(s, x + 22, y + 17, "U", 0xFFFFFF);
    gfx_text_bold(s, x + 52, y + 9, "user", 0xFFFFFF);
    gfx_text(s, x + 52, y + 26, "user@novaos", RGB(180, 186, 200));

    for (int i = 0; i < NMENU; i++) {
        int iy = menu_item_y(i);
        if (!menu_items[i].label) {
            gfx_hline(s, x + 12, iy + 5, MENU_W - 24, UI_BORDER);
            continue;
        }
        if (i == menu_hover) gfx_round_rect(s, x + 6, iy + 1, MENU_W - 12, MENU_ITEM_H - 2, 5, RGB(222, 229, 252));
        ui_icon(s, x + 16, iy + 9, menu_items[i].icon, 16);
        gfx_text(s, x + 44, iy + 9, menu_items[i].label, UI_TEXT);
    }
}

static void compose(void)
{
    for (struct window *w = windows; w; w = w->next)
        if (w->dirty && w->on_paint && !w->closing) { w->dirty = false; w->on_paint(w); }

    gfx_blit(&screen, wallpaper, 0, 0);
    draw_desktop_icons();
    struct window *focus = wm_focused();
    for (struct window *w = windows; w; w = w->next)
        if (!w->minimized && !w->closing) draw_window(w, w == focus);
    draw_taskbar();
    if (menu_open) draw_menu();

    gfx_present(0, 0, screen.w, screen.h);
    gfx_draw_cursor(mouse.x, mouse.y);
    dirty = false;
}

/* ---------- input ---------- */

static struct window *window_at(int x, int y)
{
    struct window *hit = NULL;
    for (struct window *w = windows; w; w = w->next)
        if (!w->minimized && !w->closing &&
            x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h)
            hit = w;
    return hit;
}

static int menu_item_at(int x, int y)
{
    if (x < MENU_X || x >= MENU_X + MENU_W) return -1;
    for (int i = 0; i < NMENU; i++) {
        if (!menu_items[i].label) continue;
        int iy = menu_item_y(i);
        if (y >= iy && y < iy + MENU_ITEM_H) return i;
    }
    return -1;
}

static bool in_menu(int x, int y)
{
    return x >= MENU_X && x < MENU_X + MENU_W && y >= menu_y() && y < menu_y() + menu_h();
}

static void menu_activate(int i)
{
    menu_open = false;
    dirty = true;
    int icon = menu_items[i].icon;
    if (icon == ICON_REBOOT) sys_reboot();
    else if (icon == ICON_POWER) sys_shutdown();
    else app_launch(icon, NULL);
}

static void on_left_down(int mx, int my, bool dbl)
{
    if (menu_open) {
        if (in_menu(mx, my)) {
            int i = menu_item_at(mx, my);
            if (i >= 0) menu_activate(i);
            return;
        }
        menu_open = false;
        dirty = true;
        if (my >= screen.h - TASKBAR_H && mx < 96) return;   /* start button toggles */
    }

    if (my >= screen.h - TASKBAR_H) {
        if (mx < 96) { menu_open = true; menu_hover = -1; dirty = true; return; }
        int x = 104, bw = taskbar_btn_w();
        struct window *focus = wm_focused();
        for (struct window *w = windows; w; w = w->next) {
            if (w->closing) continue;
            if (mx >= x && mx < x + bw) {
                if (w == focus) w->minimized = true;
                else wm_focus(w);
                dirty = true;
                return;
            }
            x += bw + 6;
        }
        return;
    }

    struct window *w = window_at(mx, my);
    if (w) {
        if (w != wm_focused()) wm_focus(w);
        if (my < w->y + TITLE_H) {
            int bx = w->x + w->w - 26;
            if (mx >= bx && mx < bx + 18) { wm_close(w); return; }
            if (mx >= bx - 24 && mx < bx - 6) { w->minimized = true; dirty = true; return; }
            dragging = w;
            drag_dx = mx - w->x;
            drag_dy = my - w->y;
            return;
        }
        capture = w;
        if (w->on_mouse)
            w->on_mouse(w, mx - w->x - BORDER, my - w->y - TITLE_H, mouse.buttons,
                        dbl ? MOUSE_DBLCLICK : MOUSE_DOWN);
        return;
    }

    /* desktop */
    int prev = icon_selected;
    icon_selected = -1;
    for (int i = 0; i < NDESK; i++) {
        int x, y;
        desk_item_pos(i, &x, &y);
        if (mx >= x - 20 && mx < x + 68 && my >= y - 8 && my < y + 76) {
            icon_selected = i;
            if (dbl && prev == i) app_launch(desk_items[i].icon, NULL);
        }
    }
    dirty = true;
}

static void handle_mouse(void)
{
    struct mouse_state prev = mouse;
    if (!mouse_poll(&mouse)) return;
    bool moved = mouse.x != prev.x || mouse.y != prev.y;
    int pressed = mouse.buttons & ~prev_buttons;
    int released = prev_buttons & ~mouse.buttons;
    prev_buttons = mouse.buttons;

    if (pressed & 1) {
        uint32_t now = timer_ticks();
        int ddx = mouse.x - last_click_x, ddy = mouse.y - last_click_y;
        bool dbl = (now - last_click_tick) < 45 && ddx * ddx + ddy * ddy < 36;
        last_click_tick = dbl ? 0 : now;
        last_click_x = mouse.x;
        last_click_y = mouse.y;
        on_left_down(mouse.x, mouse.y, dbl);
    } else if (pressed & 2) {
        struct window *w = window_at(mouse.x, mouse.y);
        if (w && w->on_mouse && mouse.y >= w->y + TITLE_H)
            w->on_mouse(w, mouse.x - w->x - BORDER, mouse.y - w->y - TITLE_H, mouse.buttons, MOUSE_DOWN);
    }

    if (moved) {
        if (dragging) {
            dragging->x = mouse.x - drag_dx;
            dragging->y = mouse.y - drag_dy;
            if (dragging->y < 0) dragging->y = 0;
            if (dragging->y > screen.h - TASKBAR_H - TITLE_H) dragging->y = screen.h - TASKBAR_H - TITLE_H;
            dirty = true;
        } else if (capture && capture->on_mouse && mouse.buttons) {
            capture->on_mouse(capture, mouse.x - capture->x - BORDER, mouse.y - capture->y - TITLE_H,
                              mouse.buttons, MOUSE_MOVE);
        }
        if (menu_open) {
            int h = in_menu(mouse.x, mouse.y) ? menu_item_at(mouse.x, mouse.y) : -1;
            if (h != menu_hover) { menu_hover = h; dirty = true; }
        }
    }

    if (released & 1) {
        if (capture && capture->on_mouse)
            capture->on_mouse(capture, mouse.x - capture->x - BORDER, mouse.y - capture->y - TITLE_H,
                              mouse.buttons, MOUSE_UP);
        dragging = NULL;
        capture = NULL;
    }

    if (moved && !dirty) {
        /* cheap path: restore the scene under the old cursor, draw the new one */
        gfx_present(prev.x, prev.y, 12, 19);
        gfx_draw_cursor(mouse.x, mouse.y);
    }
}

static void handle_keys(void)
{
    struct key_event ev;
    while (keyboard_poll(&ev)) {
        if (ev.key == KEY_SUPER) { menu_open = !menu_open; menu_hover = -1; dirty = true; continue; }
        if (menu_open && ev.key == 27) { menu_open = false; dirty = true; continue; }
        if ((ev.mods & KMOD_CTRL) && (ev.mods & KMOD_ALT) && (ev.key == 't' || ev.key == 'T')) {
            app_terminal();
            continue;
        }
        struct window *f = wm_focused();
        if ((ev.mods & KMOD_ALT) && ev.key == KEY_F4) { if (f) wm_close(f); continue; }
        if ((ev.mods & KMOD_ALT) && ev.key == '\t') {     /* cycle windows */
            if (windows && windows->next) wm_focus(windows);
            continue;
        }
        if (f && f->on_key) f->on_key(f, &ev);
    }
}

void wm_init(void)
{
    build_wallpaper();
    mouse_poll(&mouse);
    dirty = true;
}

void wm_run(void)
{
    uint32_t last_tick = 0;
    for (;;) {
        handle_keys();
        handle_mouse();

        uint32_t now = timer_ticks();
        if (now - last_tick >= 10) {
            last_tick = now;
            for (struct window *w = windows; w; w = w->next)
                if (w->on_tick && !w->closing) w->on_tick(w);
            struct datetime dt;
            rtc_read(&dt);
            if (dt.minute != last_minute) dirty = true;
        }

        reap_windows();
        for (struct window *w = windows; w; w = w->next)
            if (w->dirty) dirty = true;
        if (dirty) compose();
        hlt();
    }
}
