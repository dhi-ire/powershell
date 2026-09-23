/* Framebuffer setup and 2D drawing primitives. */
#include "gfx.h"
#include "font.h"
#include "heap.h"
#include "lib.h"
#include "io.h"

struct surface screen;

static uint8_t *fb;
static uint32_t fb_pitch, fb_bpp;
static char mode_name[48];

struct mb_info {
    uint32_t flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr;
    uint32_t syms[4], mmap_length, mmap_addr, drives_length, drives_addr;
    uint32_t config_table, boot_loader_name, apm_table;
    uint32_t vbe_control_info, vbe_mode_info;
    uint16_t vbe_mode, vbe_seg, vbe_off, vbe_len;
    uint64_t fb_addr;
    uint32_t fb_pitch, fb_width, fb_height;
    uint8_t fb_bpp, fb_type;
} __attribute__((packed));

static uint32_t pci_read(int bus, int dev, int fn, int off)
{
    outl(0xCF8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}

/* Bochs/QEMU "BGA" display adapter, used when the bootloader gave us no framebuffer. */
static bool bga_init(int w, int h)
{
    uint32_t bar = 0;
    for (int dev = 0; dev < 32 && !bar; dev++) {
        uint32_t id = pci_read(0, dev, 0, 0);
        if (id == 0x11111234) bar = pci_read(0, dev, 0, 0x10) & ~0xFu;
    }
    if (!bar) return false;
    #define BGA(i, v) do { outw(0x1CE, i); outw(0x1CF, v); } while (0)
    BGA(4, 0);
    BGA(1, w);
    BGA(2, h);
    BGA(3, 32);
    BGA(4, 0x41);
    #undef BGA
    fb = (uint8_t *)bar;
    fb_pitch = w * 4;
    fb_bpp = 32;
    screen.w = w;
    screen.h = h;
    snprintf(mode_name, sizeof(mode_name), "%dx%dx32 (BGA)", w, h);
    return true;
}

bool gfx_init(uint32_t magic, void *info)
{
    struct mb_info *mb = info;
    if (magic == 0x2BADB002 && (mb->flags & (1 << 12)) && mb->fb_type == 1 &&
        (mb->fb_bpp == 32 || mb->fb_bpp == 24)) {
        fb = (uint8_t *)(uint32_t)mb->fb_addr;
        fb_pitch = mb->fb_pitch;
        fb_bpp = mb->fb_bpp;
        screen.w = mb->fb_width;
        screen.h = mb->fb_height;
        snprintf(mode_name, sizeof(mode_name), "%ux%ux%u (VBE)", mb->fb_width, mb->fb_height, fb_bpp);
    } else if (!bga_init(1024, 768)) {
        return false;
    }
    screen.px = kmalloc(screen.w * screen.h * 4);
    return screen.px != NULL;
}

const char *gfx_mode_name(void) { return mode_name; }

struct surface *surface_new(int w, int h)
{
    struct surface *s = kmalloc(sizeof(*s));
    if (!s) return NULL;
    s->px = kmalloc(w * h * 4);
    if (!s->px) { kfree(s); return NULL; }
    s->w = w;
    s->h = h;
    return s;
}

void surface_free(struct surface *s)
{
    if (!s) return;
    kfree(s->px);
    kfree(s);
}

static inline bool clip(struct surface *s, int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > s->w) *w = s->w - *x;
    if (*y + *h > s->h) *h = s->h - *y;
    return *w > 0 && *h > 0;
}

void gfx_pixel(struct surface *s, int x, int y, uint32_t c)
{
    if (x >= 0 && y >= 0 && x < s->w && y < s->h) s->px[y * s->w + x] = c;
}

void gfx_fill(struct surface *s, int x, int y, int w, int h, uint32_t c)
{
    if (!clip(s, &x, &y, &w, &h)) return;
    for (int j = 0; j < h; j++) memset32(&s->px[(y + j) * s->w + x], c, w);
}

void gfx_hline(struct surface *s, int x, int y, int w, uint32_t c) { gfx_fill(s, x, y, w, 1, c); }
void gfx_vline(struct surface *s, int x, int y, int h, uint32_t c) { gfx_fill(s, x, y, 1, h, c); }

void gfx_rect(struct surface *s, int x, int y, int w, int h, uint32_t c)
{
    gfx_hline(s, x, y, w, c);
    gfx_hline(s, x, y + h - 1, w, c);
    gfx_vline(s, x, y, h, c);
    gfx_vline(s, x + w - 1, y, h, c);
}

void gfx_line(struct surface *s, int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(s, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_fill_circle(struct surface *s, int cx, int cy, int r, uint32_t c)
{
    for (int y = -r; y <= r; y++) {
        int w = 0;
        while ((w + 1) * (w + 1) + y * y <= r * r) w++;
        gfx_hline(s, cx - w, cy + y, 2 * w + 1, c);
    }
}

void gfx_thick_line(struct surface *s, int x0, int y0, int x1, int y1, int r, uint32_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (r <= 0) gfx_pixel(s, x0, y0, c); else gfx_fill_circle(s, x0, y0, r, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

uint32_t gfx_blend(uint32_t a, uint32_t b, int alpha)
{
    uint32_t rb = (((b & 0xFF00FF) * alpha) + ((a & 0xFF00FF) * (255 - alpha))) >> 8;
    uint32_t g  = (((b & 0x00FF00) * alpha) + ((a & 0x00FF00) * (255 - alpha))) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

void gfx_gradient(struct surface *s, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    for (int j = 0; j < h; j++)
        gfx_hline(s, x, y + j, w, gfx_blend(top, bottom, h > 1 ? j * 255 / (h - 1) : 0));
}

void gfx_round_rect(struct surface *s, int x, int y, int w, int h, int r, uint32_t c)
{
    for (int j = 0; j < h; j++) {
        int inset = 0;
        int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
        if (dy) {
            int dx = 0;
            while (dx < r && (r - dx) * (r - dx) + dy * dy > r * r) dx++;
            inset = dx;
        }
        gfx_hline(s, x + inset, y + j, w - 2 * inset, c);
    }
}

void gfx_shadow(struct surface *s, int x, int y, int w, int h)
{
    for (int i = 1; i <= 6; i++) {
        int alpha = 60 - i * 9;
        int yy = y + h + i - 1, xx = x + w + i - 1;
        for (int k = x + i; k < x + w + i; k++)
            if (k >= 0 && k < s->w && yy >= 0 && yy < s->h)
                s->px[yy * s->w + k] = gfx_blend(s->px[yy * s->w + k], 0, alpha);
        for (int k = y + i; k < y + h + i - 1; k++)
            if (xx >= 0 && xx < s->w && k >= 0 && k < s->h)
                s->px[k * s->w + xx] = gfx_blend(s->px[k * s->w + xx], 0, alpha);
    }
}

void gfx_char(struct surface *s, int x, int y, char ch, uint32_t fg)
{
    uint8_t c = (uint8_t)ch;
    if (c >= 128) c = '?';
    const unsigned char *g = font8x16[c];
    if (x >= 0 && y >= 0 && x + FONT_W <= s->w && y + FONT_H <= s->h) {
        uint32_t *row = &s->px[y * s->w + x];
        for (int j = 0; j < FONT_H; j++, row += s->w) {
            uint8_t bits = g[j];
            for (int i = 0; i < FONT_W; i++)
                if (bits & (0x80 >> i)) row[i] = fg;
        }
        return;
    }
    for (int j = 0; j < FONT_H; j++)
        for (int i = 0; i < FONT_W; i++)
            if (g[j] & (0x80 >> i)) gfx_pixel(s, x + i, y + j, fg);
}

void gfx_text(struct surface *s, int x, int y, const char *str, uint32_t fg)
{
    for (; *str; str++, x += FONT_W) gfx_char(s, x, y, *str, fg);
}

void gfx_text_bold(struct surface *s, int x, int y, const char *str, uint32_t fg)
{
    gfx_text(s, x, y, str, fg);
    gfx_text(s, x + 1, y, str, fg);
}

int gfx_text_width(const char *str) { return strlen(str) * FONT_W; }

void gfx_blit(struct surface *dst, const struct surface *src, int dx, int dy)
{
    int sx = 0, sy = 0, w = src->w, h = src->h;
    if (dx < 0) { sx = -dx; w += dx; dx = 0; }
    if (dy < 0) { sy = -dy; h += dy; dy = 0; }
    if (dx + w > dst->w) w = dst->w - dx;
    if (dy + h > dst->h) h = dst->h - dy;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++)
        memcpy(&dst->px[(dy + j) * dst->w + dx], &src->px[(sy + j) * src->w + sx], w * 4);
}

void gfx_present(int x, int y, int w, int h)
{
    if (!clip(&screen, &x, &y, &w, &h)) return;
    for (int j = y; j < y + h; j++) {
        uint32_t *src = &screen.px[j * screen.w + x];
        uint8_t *dst = fb + j * fb_pitch + x * (fb_bpp / 8);
        if (fb_bpp == 32) {
            memcpy(dst, src, w * 4);
        } else {
            for (int i = 0; i < w; i++, dst += 3) {
                dst[0] = src[i]; dst[1] = src[i] >> 8; dst[2] = src[i] >> 16;
            }
        }
    }
}

static inline void fb_pixel(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= screen.w || y >= screen.h) return;
    uint8_t *p = fb + y * fb_pitch + x * (fb_bpp / 8);
    if (fb_bpp == 32) *(uint32_t *)p = c;
    else { p[0] = c; p[1] = c >> 8; p[2] = c >> 16; }
}

/* 12x19 arrow: '#' outline, '.' fill */
static const char *cursor_img[19] = {
    "#           ",
    "##          ",
    "#.#         ",
    "#..#        ",
    "#...#       ",
    "#....#      ",
    "#.....#     ",
    "#......#    ",
    "#.......#   ",
    "#........#  ",
    "#.........# ",
    "#......#####",
    "#...#..#    ",
    "#..# #..#   ",
    "#.#  #..#   ",
    "##    #..#  ",
    "#     #..#  ",
    "       ##   ",
    "            ",
};

void gfx_draw_cursor(int x, int y)
{
    for (int j = 0; j < 19; j++)
        for (int i = 0; i < 12; i++) {
            char c = cursor_img[j][i];
            if (c == '#') fb_pixel(x + i, y + j, 0x000000);
            else if (c == '.') fb_pixel(x + i, y + j, 0xFFFFFF);
        }
}

void gfx_panic_screen(const char *msg)
{
    if (!fb || !screen.px) {
        volatile uint16_t *vga = (uint16_t *)0xB8000;
        const char *hdr = "KERNEL PANIC: ";
        int i = 0;
        for (; hdr[i]; i++) vga[i] = 0x4F00 | hdr[i];
        for (int k = 0; msg[k] && i < 80 * 25; k++, i++) vga[i] = 0x4F00 | (uint8_t)msg[k];
        return;
    }
    gfx_fill(&screen, 0, 0, screen.w, screen.h, RGB(0, 90, 170));
    int x = screen.w / 2 - 300, y = screen.h / 2 - 80;
    gfx_text_bold(&screen, x, y, ":(  NovaOS ran into a problem", 0xFFFFFF);
    gfx_text(&screen, x, y + 40, msg, 0xFFFFFF);
    gfx_text(&screen, x, y + 80, "The system has been halted. Please restart your computer.", RGB(200, 220, 255));
    gfx_present(0, 0, screen.w, screen.h);
}
