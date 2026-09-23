#ifndef GFX_H
#define GFX_H

#include "types.h"

struct surface {
    uint32_t *px;
    int w, h;
};

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

extern struct surface screen;      /* composited scene (without the cursor) */

bool gfx_init(uint32_t mb_magic, void *mb_info);
const char *gfx_mode_name(void);

struct surface *surface_new(int w, int h);
void surface_free(struct surface *s);

void gfx_pixel(struct surface *s, int x, int y, uint32_t c);
void gfx_fill(struct surface *s, int x, int y, int w, int h, uint32_t c);
void gfx_rect(struct surface *s, int x, int y, int w, int h, uint32_t c);
void gfx_hline(struct surface *s, int x, int y, int w, uint32_t c);
void gfx_vline(struct surface *s, int x, int y, int h, uint32_t c);
void gfx_line(struct surface *s, int x0, int y0, int x1, int y1, uint32_t c);
void gfx_fill_circle(struct surface *s, int cx, int cy, int r, uint32_t c);
void gfx_thick_line(struct surface *s, int x0, int y0, int x1, int y1, int r, uint32_t c);
void gfx_gradient(struct surface *s, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void gfx_round_rect(struct surface *s, int x, int y, int w, int h, int r, uint32_t c);
void gfx_char(struct surface *s, int x, int y, char ch, uint32_t fg);
void gfx_text(struct surface *s, int x, int y, const char *str, uint32_t fg);
void gfx_text_bold(struct surface *s, int x, int y, const char *str, uint32_t fg);
int  gfx_text_width(const char *str);
void gfx_blit(struct surface *dst, const struct surface *src, int dx, int dy);
void gfx_shadow(struct surface *s, int x, int y, int w, int h);
uint32_t gfx_blend(uint32_t a, uint32_t b, int alpha);   /* alpha 0..255 of b over a */

/* Copy part of the scene to the real framebuffer, then draw cursor on top. */
void gfx_present(int x, int y, int w, int h);
void gfx_draw_cursor(int x, int y);
void gfx_panic_screen(const char *msg);

#endif
