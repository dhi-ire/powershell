/* Desktop applications: Files, Text Editor, Paint, Calculator, About. */
#include "wm.h"
#include "fs.h"
#include "lib.h"
#include "heap.h"
#include "font.h"
#include "cpu.h"
#include "sysinfo.h"

void app_launch(int icon, const char *arg)
{
    switch (icon) {
    case ICON_TERMINAL: app_terminal(); break;
    case ICON_FILES:    app_files(arg); break;
    case ICON_EDITOR:   app_editor(arg); break;
    case ICON_PAINT:    app_paint(); break;
    case ICON_CALC:     app_calc(); break;
    case ICON_ABOUT:    app_about(); break;
    }
}

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/* =====================================================================
 * Files
 * ===================================================================== */

#define FM_W 600
#define FM_H 420
#define FM_BAR 44
#define FM_ROW 26

struct files {
    struct fs_node *dir;
    int selected, scroll;
    int gen;
};

static int fm_visible_rows(void) { return (FM_H - FM_BAR - 28) / FM_ROW; }

static struct fs_node *fm_entry(struct files *f, int idx)
{
    struct fs_node *n = f->dir->child;
    while (n && idx--) n = n->next;
    return n;
}

static void fm_paint(struct window *w)
{
    struct files *f = w->data;
    struct surface *s = w->canvas;
    gfx_fill(s, 0, 0, s->w, s->h, 0xFFFFFF);
    gfx_fill(s, 0, 0, s->w, FM_BAR, UI_BG);
    gfx_hline(s, 0, FM_BAR - 1, s->w, UI_BORDER);
    ui_button(s, 8, 8, 44, 28, "Up", false);
    char path[256];
    fs_path(f->dir, path, sizeof(path));
    gfx_round_rect(s, 60, 8, 270, 28, 4, UI_BORDER);
    gfx_round_rect(s, 61, 9, 268, 26, 3, 0xFFFFFF);
    char shown[34];
    strlcpy(shown, path, sizeof(shown));
    gfx_text(s, 70, 14, shown, UI_TEXT);
    ui_button(s, 338, 8, 90, 28, "New Folder", false);
    ui_button(s, 434, 8, 76, 28, "New File", false);
    ui_button(s, 516, 8, 76, 28, "Delete", false);

    int y = FM_BAR + 4, i = 0, rows = fm_visible_rows();
    for (struct fs_node *n = f->dir->child; n; n = n->next, i++) {
        if (i < f->scroll) continue;
        if (i >= f->scroll + rows) break;
        if (i == f->selected) gfx_fill(s, 4, y, s->w - 8, FM_ROW, RGB(222, 229, 252));
        ui_icon(s, 12, y + 5, n->is_dir ? ICON_FOLDER : ICON_FILE, 16);
        gfx_text(s, 38, y + 5, n->name, UI_TEXT);
        char sz[24];
        if (n->is_dir) snprintf(sz, sizeof(sz), "%d items", fs_count(n));
        else snprintf(sz, sizeof(sz), "%u bytes", n->size);
        gfx_text(s, 330, y + 5, sz, UI_MUTED);
        gfx_text(s, 470, y + 5, n->is_dir ? "Folder" : "Text file", UI_MUTED);
        y += FM_ROW;
    }
    if (!f->dir->child) gfx_text(s, 20, FM_BAR + 16, "This folder is empty.", UI_MUTED);

    gfx_fill(s, 0, s->h - 24, s->w, 24, UI_BG);
    gfx_hline(s, 0, s->h - 24, s->w, UI_BORDER);
    char st[64];
    snprintf(st, sizeof(st), "%d items   -   double-click to open", fs_count(f->dir));
    gfx_text(s, 10, s->h - 20, st, UI_MUTED);
}

static void fm_set_dir(struct window *w, struct fs_node *d)
{
    struct files *f = w->data;
    f->dir = d;
    f->selected = -1;
    f->scroll = 0;
    char path[256], title[64];
    fs_path(d, path, sizeof(path));
    snprintf(title, sizeof(title), "Files - %s", path);
    wm_set_title(w, title);
    wm_invalidate(w);
}

static void fm_open(struct window *w, int idx)
{
    struct files *f = w->data;
    struct fs_node *n = fm_entry(f, idx);
    if (!n) return;
    if (n->is_dir) { fm_set_dir(w, n); return; }
    char p[256];
    fs_path(n, p, sizeof(p));
    app_editor(p);
}

static void fm_new(struct window *w, bool dir)
{
    struct files *f = w->data;
    char name[FS_NAME_MAX];
    for (int i = 1; i < 100; i++) {
        const char *base = dir ? "New Folder" : "new_file";
        if (i == 1) snprintf(name, sizeof(name), dir ? "%s" : "%s.txt", base);
        else snprintf(name, sizeof(name), dir ? "%s %d" : "%s_%d.txt", base, i);
        if (fs_create(f->dir, name, dir)) break;
    }
    wm_invalidate(w);
}

static void fm_mouse(struct window *w, int x, int y, int buttons, int ev)
{
    (void)buttons;
    struct files *f = w->data;
    if (ev != MOUSE_DOWN && ev != MOUSE_DBLCLICK) return;
    if (y < FM_BAR) {
        if (in_rect(x, y, 8, 8, 44, 28)) fm_set_dir(w, f->dir->parent);
        else if (in_rect(x, y, 338, 8, 90, 28)) fm_new(w, true);
        else if (in_rect(x, y, 434, 8, 76, 28)) fm_new(w, false);
        else if (in_rect(x, y, 516, 8, 76, 28)) {
            struct fs_node *n = fm_entry(f, f->selected);
            if (n) { fs_remove(n, true); f->selected = -1; wm_invalidate(w); }
        }
        return;
    }
    int idx = (y - FM_BAR - 4) / FM_ROW + f->scroll;
    if (y < FM_BAR + 4 || idx >= fs_count(f->dir) || y >= FM_H - 24) { f->selected = -1; wm_invalidate(w); return; }
    if (ev == MOUSE_DBLCLICK && idx == f->selected) { fm_open(w, idx); return; }
    f->selected = idx;
    wm_invalidate(w);
}

static void fm_key(struct window *w, struct key_event *ev)
{
    struct files *f = w->data;
    int count = fs_count(f->dir);
    switch (ev->key) {
    case KEY_UP: if (f->selected > 0) f->selected--; break;
    case KEY_DOWN: if (f->selected < count - 1) f->selected++; break;
    case '\n': fm_open(w, f->selected); return;
    case '\b': fm_set_dir(w, f->dir->parent); return;
    case KEY_DEL: {
        struct fs_node *n = fm_entry(f, f->selected);
        if (n) { fs_remove(n, true); f->selected = -1; }
        break;
    }
    default: return;
    }
    int rows = fm_visible_rows();
    if (f->selected >= 0 && f->selected < f->scroll) f->scroll = f->selected;
    if (f->selected >= f->scroll + rows) f->scroll = f->selected - rows + 1;
    wm_invalidate(w);
}

static void fm_tick(struct window *w)
{
    /* cheap change detection: re-paint if the entry count changed (e.g. from the shell) */
    struct files *f = w->data;
    int c = fs_count(f->dir);
    if (c != f->gen) { f->gen = c; wm_invalidate(w); }
}

static void free_data(struct window *w) { kfree(w->data); }

void app_files(const char *path)
{
    struct files *f = kcalloc(sizeof(*f));
    struct window *w = f ? wm_create("Files", ICON_FILES, FM_W, FM_H) : NULL;
    if (!w) { kfree(f); return; }
    w->data = f;
    w->on_paint = fm_paint;
    w->on_mouse = fm_mouse;
    w->on_key = fm_key;
    w->on_tick = fm_tick;
    w->on_close = free_data;
    struct fs_node *d = fs_resolve(fs_root(), path ? path : "/home/user");
    fm_set_dir(w, d && d->is_dir ? d : fs_root());
    f->gen = fs_count(f->dir);
}

/* =====================================================================
 * Text Editor
 * ===================================================================== */

#define ED_W 640
#define ED_H 440
#define ED_BAR 40
#define ED_STATUS 24
#define ED_MAX 32768

struct editor {
    char path[256];
    char *buf;
    int len, cur, top;
    bool modified, blink;
    char msg[48];
};

static int ed_rows(void) { return (ED_H - ED_BAR - ED_STATUS - 8) / FONT_H; }
static int ed_cols(void) { return (ED_W - 16 - 40) / FONT_W; }

static int line_start(struct editor *e, int pos) { while (pos > 0 && e->buf[pos - 1] != '\n') pos--; return pos; }
static int line_end(struct editor *e, int pos)   { while (pos < e->len && e->buf[pos] != '\n') pos++; return pos; }
static int line_of(struct editor *e, int pos)    { int l = 0; for (int i = 0; i < pos; i++) if (e->buf[i] == '\n') l++; return l; }
static int pos_of_line(struct editor *e, int line)
{
    int p = 0;
    while (line > 0 && p < e->len) { if (e->buf[p++] == '\n') line--; }
    return p;
}

static void ed_update_title(struct window *w)
{
    struct editor *e = w->data;
    const char *name = e->path[0] ? strrchr(e->path, '/') + 1 : "Untitled";
    char t[64];
    snprintf(t, sizeof(t), "%s%s - Text Editor", e->modified ? "*" : "", name);
    wm_set_title(w, t);
}

static void ed_paint(struct window *w)
{
    struct editor *e = w->data;
    struct surface *s = w->canvas;
    gfx_fill(s, 0, 0, s->w, s->h, 0xFFFFFF);
    gfx_fill(s, 0, 0, s->w, ED_BAR, UI_BG);
    gfx_hline(s, 0, ED_BAR - 1, s->w, UI_BORDER);
    ui_button(s, 8, 6, 70, 28, "Save", true);
    ui_button(s, 84, 6, 70, 28, "New", false);
    gfx_text(s, 168, 12, e->path[0] ? e->path : "(unsaved - will save to ~/Documents)", UI_MUTED);

    int rows = ed_rows(), cols = ed_cols();
    int cur_line = line_of(e, e->cur);
    gfx_fill(s, 0, ED_BAR, 40, s->h - ED_BAR - ED_STATUS, RGB(240, 242, 247));
    int p = pos_of_line(e, e->top);
    for (int r = 0; r < rows && p <= e->len; r++) {
        int y = ED_BAR + 4 + r * FONT_H;
        char num[8];
        snprintf(num, sizeof(num), "%3d", e->top + r + 1);
        gfx_text(s, 6, y, num, e->top + r == cur_line ? UI_TEXT : RGB(170, 176, 190));
        if (e->top + r == cur_line) gfx_fill(s, 40, y, s->w - 40, FONT_H, RGB(247, 249, 255));
        int x = 0;
        while (p < e->len && e->buf[p] != '\n') {
            if (x < cols) gfx_char(s, 48 + x * FONT_W, y, e->buf[p], UI_TEXT);
            x++; p++;
        }
        if (p >= e->len) break;
        p++;
    }
    /* cursor */
    if (e->blink && w == wm_focused()) {
        int cl = cur_line - e->top, cc = e->cur - line_start(e, e->cur);
        if (cl >= 0 && cl < rows && cc < cols)
            gfx_fill(s, 48 + cc * FONT_W, ED_BAR + 4 + cl * FONT_H, 2, FONT_H, UI_ACCENT);
    }

    gfx_fill(s, 0, s->h - ED_STATUS, s->w, ED_STATUS, UI_BG);
    gfx_hline(s, 0, s->h - ED_STATUS, s->w, UI_BORDER);
    char st[96];
    snprintf(st, sizeof(st), "Ln %d, Col %d   |   %d chars   |   Ctrl+S save   %s",
             cur_line + 1, e->cur - line_start(e, e->cur) + 1, e->len, e->msg);
    gfx_text(s, 10, s->h - 20, st, UI_MUTED);
}

static void ed_scroll_to_cursor(struct editor *e)
{
    int l = line_of(e, e->cur), rows = ed_rows();
    if (l < e->top) e->top = l;
    if (l >= e->top + rows) e->top = l - rows + 1;
}

static void ed_save(struct window *w)
{
    struct editor *e = w->data;
    struct fs_node *n = NULL;
    if (!e->path[0]) {
        struct fs_node *docs = fs_resolve(fs_root(), "/home/user/Documents");
        for (int i = 1; i < 100 && !n; i++) {
            char name[32];
            snprintf(name, sizeof(name), i == 1 ? "untitled.txt" : "untitled_%d.txt", i);
            n = fs_create(docs, name, false);
        }
        if (n) fs_path(n, e->path, sizeof(e->path));
    } else {
        n = fs_resolve(fs_root(), e->path);
        if (!n) n = fs_create(fs_root(), e->path, false);
    }
    if (n && !fs_write(n, e->buf, e->len)) {
        e->modified = false;
        strcpy(e->msg, "Saved.");
    } else {
        strcpy(e->msg, "Save failed!");
    }
    ed_update_title(w);
    wm_invalidate(w);
}

static void ed_insert(struct editor *e, char c)
{
    if (e->len >= ED_MAX - 1) return;
    memmove(e->buf + e->cur + 1, e->buf + e->cur, e->len - e->cur);
    e->buf[e->cur++] = c;
    e->len++;
    e->modified = true;
}

static void ed_key(struct window *w, struct key_event *ev)
{
    struct editor *e = w->data;
    int k = ev->key;
    bool was_modified = e->modified;
    e->msg[0] = 0;
    e->blink = true;
    if ((ev->mods & KMOD_CTRL) && (k == 's' || k == 'S')) { ed_save(w); return; }
    if ((ev->mods & KMOD_CTRL) && (k == KEY_HOME || k == KEY_END)) {
        e->cur = k == KEY_HOME ? 0 : e->len;
        k = 0;
    }
    switch (k) {
    case '\b':
        if (e->cur > 0) {
            memmove(e->buf + e->cur - 1, e->buf + e->cur, e->len - e->cur);
            e->cur--; e->len--; e->modified = true;
        }
        break;
    case KEY_DEL:
        if (e->cur < e->len) {
            memmove(e->buf + e->cur, e->buf + e->cur + 1, e->len - e->cur - 1);
            e->len--; e->modified = true;
        }
        break;
    case KEY_LEFT:  if (e->cur > 0) e->cur--; break;
    case KEY_RIGHT: if (e->cur < e->len) e->cur++; break;
    case KEY_HOME:  e->cur = line_start(e, e->cur); break;
    case KEY_END:   e->cur = line_end(e, e->cur); break;
    case KEY_UP:
    case KEY_DOWN:
    case KEY_PGUP:
    case KEY_PGDN: {
        int col = e->cur - line_start(e, e->cur);
        int steps = (k == KEY_PGUP || k == KEY_PGDN) ? ed_rows() - 1 : 1;
        bool up = k == KEY_UP || k == KEY_PGUP;
        int ls = line_start(e, e->cur);
        for (int i = 0; i < steps; i++) {
            if (up) { if (ls == 0) break; ls = line_start(e, ls - 1); }
            else { int le = line_end(e, ls); if (le >= e->len) break; ls = le + 1; }
        }
        int le = line_end(e, ls);
        e->cur = ls + col > le ? le : ls + col;
        break;
    }
    case '\t': for (int i = 0; i < 4; i++) ed_insert(e, ' '); break;
    default:
        if (k == '\n' || (k >= 32 && k < 127)) ed_insert(e, (char)k);
    }
    ed_scroll_to_cursor(e);
    if (was_modified != e->modified) ed_update_title(w);
    wm_invalidate(w);
}

static void ed_mouse(struct window *w, int x, int y, int buttons, int ev)
{
    (void)buttons;
    struct editor *e = w->data;
    if (ev != MOUSE_DOWN) return;
    if (y < ED_BAR) {
        if (in_rect(x, y, 8, 6, 70, 28)) ed_save(w);
        else if (in_rect(x, y, 84, 6, 70, 28)) app_editor(NULL);
        return;
    }
    int line = e->top + (y - ED_BAR - 4) / FONT_H;
    int col = (x - 48 + FONT_W / 2) / FONT_W;
    if (col < 0) col = 0;
    int nlines = line_of(e, e->len);
    if (line > nlines) line = nlines;
    int ls = pos_of_line(e, line), le = line_end(e, ls);
    e->cur = ls + col > le ? le : ls + col;
    e->blink = true;
    wm_invalidate(w);
}

static void ed_tick(struct window *w)
{
    struct editor *e = w->data;
    static uint32_t n;
    if (++n % 5 == 0) { e->blink = !e->blink; wm_invalidate(w); }
}

static void ed_close(struct window *w)
{
    struct editor *e = w->data;
    kfree(e->buf);
    kfree(e);
}

void app_editor(const char *path)
{
    struct editor *e = kcalloc(sizeof(*e));
    if (!e) return;
    e->buf = kmalloc(ED_MAX);
    struct window *w = e->buf ? wm_create("Text Editor", ICON_EDITOR, ED_W, ED_H) : NULL;
    if (!w) { kfree(e->buf); kfree(e); return; }
    w->data = e;
    w->on_paint = ed_paint;
    w->on_key = ed_key;
    w->on_mouse = ed_mouse;
    w->on_tick = ed_tick;
    w->on_close = ed_close;
    if (path) {
        strlcpy(e->path, path, sizeof(e->path));
        struct fs_node *n = fs_resolve(fs_root(), path);
        if (n && !n->is_dir && n->data) {
            e->len = n->size < ED_MAX - 1 ? n->size : ED_MAX - 1;
            memcpy(e->buf, n->data, e->len);
        }
    }
    e->blink = true;
    ed_update_title(w);
    wm_invalidate(w);
}

/* =====================================================================
 * Paint
 * ===================================================================== */

#define PT_W 640
#define PT_H 460
#define PT_BAR 44

static const uint32_t palette[] = {
    0x000000, 0xFFFFFF, RGB(128, 128, 128), RGB(235, 64, 52), RGB(250, 140, 30), RGB(250, 210, 40),
    RGB(60, 190, 90), RGB(40, 180, 200), RGB(60, 110, 240), RGB(140, 80, 220), RGB(230, 90, 180), RGB(120, 80, 40),
};
#define NPAL (int)(sizeof(palette) / sizeof(palette[0]))
static const int brush_sizes[] = { 1, 3, 7 };

struct paint {
    struct surface *img;
    uint32_t color;
    int brush;
    int lx, ly;
    bool drawing;
};

static void pt_paint(struct window *w)
{
    struct paint *p = w->data;
    struct surface *s = w->canvas;
    gfx_fill(s, 0, 0, s->w, PT_BAR, UI_BG);
    gfx_hline(s, 0, PT_BAR - 1, s->w, UI_BORDER);
    for (int i = 0; i < NPAL; i++) {
        int x = 8 + i * 28;
        if (palette[i] == p->color) gfx_round_rect(s, x - 2, 6, 28, 30, 4, UI_ACCENT);
        gfx_fill(s, x + 1, 9, 22, 24, UI_BORDER);
        gfx_fill(s, x + 2, 10, 20, 22, palette[i]);
    }
    int bx = 8 + NPAL * 28 + 12;
    for (int i = 0; i < 3; i++) {
        int x = bx + i * 34;
        gfx_round_rect(s, x, 7, 30, 30, 4, p->brush == i ? RGB(222, 229, 252) : 0xFFFFFF);
        gfx_rect(s, x, 7, 30, 30, p->brush == i ? UI_ACCENT : UI_BORDER);
        gfx_fill_circle(s, x + 15, 22, brush_sizes[i] < 2 ? 1 : brush_sizes[i], UI_TEXT);
    }
    ui_button(s, s->w - 80, 8, 70, 28, "Clear", false);
    gfx_blit(s, p->img, 0, PT_BAR);
}

static void pt_mouse(struct window *w, int x, int y, int buttons, int ev)
{
    struct paint *p = w->data;
    if (ev == MOUSE_UP) { p->drawing = false; return; }
    if (ev == MOUSE_DOWN && y < PT_BAR) {
        for (int i = 0; i < NPAL; i++)
            if (in_rect(x, y, 8 + i * 28, 6, 24, 30)) p->color = palette[i];
        int bx = 8 + NPAL * 28 + 12;
        for (int i = 0; i < 3; i++)
            if (in_rect(x, y, bx + i * 34, 7, 30, 30)) p->brush = i;
        if (in_rect(x, y, w->canvas->w - 80, 8, 70, 28)) gfx_fill(p->img, 0, 0, p->img->w, p->img->h, 0xFFFFFF);
        wm_invalidate(w);
        return;
    }
    int iy = y - PT_BAR;
    uint32_t c = (buttons & 2) ? 0xFFFFFF : p->color;          /* right button erases */
    int r = (buttons & 2) ? 8 : brush_sizes[p->brush] / 2 + (p->brush > 0);
    if (ev == MOUSE_DOWN || ev == MOUSE_DBLCLICK) {
        if (iy < 0) return;
        p->drawing = true;
        gfx_thick_line(p->img, x, iy, x, iy, r, c);
    } else if (ev == MOUSE_MOVE && p->drawing) {
        gfx_thick_line(p->img, p->lx, p->ly, x, iy, r, c);
    } else {
        return;
    }
    p->lx = x;
    p->ly = iy;
    wm_invalidate(w);
}

static void pt_close(struct window *w)
{
    struct paint *p = w->data;
    surface_free(p->img);
    kfree(p);
}

void app_paint(void)
{
    struct paint *p = kcalloc(sizeof(*p));
    if (!p) return;
    p->img = surface_new(PT_W, PT_H - PT_BAR);
    struct window *w = p->img ? wm_create("Paint", ICON_PAINT, PT_W, PT_H) : NULL;
    if (!w) { surface_free(p->img); kfree(p); return; }
    gfx_fill(p->img, 0, 0, p->img->w, p->img->h, 0xFFFFFF);
    p->color = palette[8];
    p->brush = 1;
    w->data = p;
    w->on_paint = pt_paint;
    w->on_mouse = pt_mouse;
    w->on_close = pt_close;
    wm_invalidate(w);
}

/* =====================================================================
 * Calculator
 * ===================================================================== */

#define CA_W 264
#define CA_H 360

struct calc {
    int32_t acc, cur;
    char op;
    bool fresh, error;
};

static const char *calc_keys[5][4] = {
    { "C", "<", "%", "/" },
    { "7", "8", "9", "*" },
    { "4", "5", "6", "-" },
    { "1", "2", "3", "+" },
    { "+/-", "0", "00", "=" },
};

static void ca_paint(struct window *w)
{
    struct calc *c = w->data;
    struct surface *s = w->canvas;
    gfx_fill(s, 0, 0, s->w, s->h, RGB(36, 40, 52));
    gfx_round_rect(s, 10, 10, s->w - 20, 64, 6, RGB(22, 25, 33));
    char buf[24];
    if (c->error) strcpy(buf, "Error");
    else snprintf(buf, sizeof(buf), "%d", c->cur);
    int tw = gfx_text_width(buf) * 2;
    /* large digits: draw each glyph 2x */
    for (int i = 0; buf[i]; i++) {
        const unsigned char *g = font8x16[(int)buf[i]];
        for (int j = 0; j < 16; j++)
            for (int k = 0; k < 8; k++)
                if (g[j] & (0x80 >> k))
                    gfx_fill(s, s->w - 24 - tw + i * 16 + k * 2, 28 + j * 2, 2, 2, 0xFFFFFF);
    }
    if (c->op) {
        char o[16];
        snprintf(o, sizeof(o), "%d %c", c->acc, c->op);
        gfx_text(s, 20, 16, o, RGB(140, 146, 160));
    }
    for (int r = 0; r < 5; r++)
        for (int k = 0; k < 4; k++) {
            int x = 10 + k * 62, y = 86 + r * 54;
            const char *l = calc_keys[r][k];
            uint32_t bg = k == 3 ? RGB(250, 150, 50) : r == 0 ? RGB(90, 96, 112) : RGB(62, 68, 84);
            if (!strcmp(l, "=")) bg = UI_ACCENT;
            gfx_round_rect(s, x, y, 56, 48, 8, bg);
            gfx_text_bold(s, x + 28 - gfx_text_width(l) / 2, y + 16, l, 0xFFFFFF);
        }
}

static int32_t ca_apply(struct calc *c, int32_t a, int32_t b, char op)
{
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': if (!b) { c->error = true; return 0; } return a / b;
    case '%': if (!b) { c->error = true; return 0; } return a % b;
    }
    return b;
}

static void ca_press(struct window *w, const char *k)
{
    struct calc *c = w->data;
    if (c->error && strcmp(k, "C")) return;
    if (isdigit(k[0])) {
        for (const char *p = k; *p; p++) {
            if (c->fresh) { c->cur = 0; c->fresh = false; }
            if (c->cur < 100000000 && c->cur > -100000000)
                c->cur = c->cur * 10 + (c->cur < 0 ? -(*p - '0') : (*p - '0'));
        }
    } else if (!strcmp(k, "C")) {
        memset(c, 0, sizeof(*c));
    } else if (!strcmp(k, "<")) {
        c->cur /= 10;
    } else if (!strcmp(k, "+/-")) {
        c->cur = -c->cur;
    } else if (!strcmp(k, "=")) {
        if (c->op) c->cur = ca_apply(c, c->acc, c->cur, c->op);
        c->op = 0;
        c->fresh = true;
    } else {
        if (c->op && !c->fresh) c->cur = ca_apply(c, c->acc, c->cur, c->op);
        c->acc = c->cur;
        c->op = k[0];
        c->fresh = true;
    }
    wm_invalidate(w);
}

static void ca_mouse(struct window *w, int x, int y, int buttons, int ev)
{
    (void)buttons;
    if (ev != MOUSE_DOWN && ev != MOUSE_DBLCLICK) return;
    for (int r = 0; r < 5; r++)
        for (int k = 0; k < 4; k++)
            if (in_rect(x, y, 10 + k * 62, 86 + r * 54, 56, 48)) ca_press(w, calc_keys[r][k]);
}

static void ca_key(struct window *w, struct key_event *ev)
{
    char k[2] = { (char)ev->key, 0 };
    if (ev->key == '\n') k[0] = '=';
    else if (ev->key == '\b') k[0] = '<';
    else if (ev->key == 27) k[0] = 'C';
    else if (ev->key == 'c' || ev->key == 'C') k[0] = 'C';
    if (strchr("0123456789+-*/%=<C", k[0]) && k[0]) ca_press(w, k);
}

void app_calc(void)
{
    struct calc *c = kcalloc(sizeof(*c));
    struct window *w = c ? wm_create("Calculator", ICON_CALC, CA_W, CA_H) : NULL;
    if (!w) { kfree(c); return; }
    w->data = c;
    w->on_paint = ca_paint;
    w->on_mouse = ca_mouse;
    w->on_key = ca_key;
    w->on_close = free_data;
    wm_invalidate(w);
}

/* =====================================================================
 * About
 * ===================================================================== */

static void ab_paint(struct window *w)
{
    struct surface *s = w->canvas;
    gfx_fill(s, 0, 0, s->w, s->h, 0xFFFFFF);
    gfx_gradient(s, 0, 0, s->w, 110, RGB(18, 24, 58), RGB(88, 40, 120));
    gfx_fill_circle(s, 60, 55, 30, RGB(120, 220, 200));
    gfx_fill_circle(s, 60, 55, 13, RGB(18, 24, 58));
    for (int i = 0; OS_NAME[i]; i++) {
        const unsigned char *g = font8x16[(int)OS_NAME[i]];
        for (int j = 0; j < 16; j++)
            for (int k = 0; k < 8; k++)
                if (g[j] & (0x80 >> k)) gfx_fill(s, 110 + i * 24 + k * 3, 24 + j * 3, 3, 3, 0xFFFFFF);
    }
    gfx_text(s, 112, 80, "Version " OS_VERSION " (" OS_CODENAME ")", RGB(200, 205, 230));

    char cpu[64], up[48], mem[64];
    uint32_t total, used;
    cpu_brand(cpu, sizeof(cpu));
    uptime_str(up, sizeof(up));
    heap_stats(&total, &used);
    snprintf(mem, sizeof(mem), "%u MB total, %u KB used", sys_mem_kb / 1024, used / 1024);
    const char *labels[] = { "Kernel", "Architecture", "Processor", "Memory", "Display", "Uptime" };
    const char *vals[] = { "nova " OS_VERSION " (monolithic)", "i686, 32-bit protected mode", cpu, mem,
                           gfx_mode_name(), up };
    for (int i = 0; i < 6; i++) {
        gfx_text_bold(s, 24, 130 + i * 24, labels[i], UI_TEXT);
        gfx_text(s, 150, 130 + i * 24, vals[i], UI_MUTED);
    }
    gfx_hline(s, 24, 280, s->w - 48, UI_BORDER);
    gfx_text(s, 24, 290, "Written from scratch in C and x86 assembly.", UI_MUTED);
    gfx_text(s, 24, 308, "Font: Terminus (SIL Open Font License 1.1).", UI_MUTED);
}

static void ab_tick(struct window *w)
{
    static uint32_t n;
    if (++n % 10 == 0) wm_invalidate(w);     /* refresh uptime every second */
}

void app_about(void)
{
    struct window *w = wm_create("About NovaOS", ICON_ABOUT, 460, 336);
    if (!w) return;
    w->on_paint = ab_paint;
    w->on_tick = ab_tick;
    wm_invalidate(w);
}
