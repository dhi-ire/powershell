/* PS/2 mouse driver. */
#include "input.h"
#include "cpu.h"
#include "io.h"

static volatile int mx, my, mbuttons, changed;
static int max_x, max_y;
static uint8_t packet[3];
static int cycle;

static void wait_write(void) { for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) ; }
static void wait_read(void)  { for (int i = 0; i < 100000 && !(inb(0x64) & 1); i++) ; }

static void mouse_write(uint8_t v)
{
    wait_write(); outb(0x64, 0xD4);
    wait_write(); outb(0x60, v);
    wait_read();  inb(0x60);           /* ACK */
}

static void mouse_irq(struct regs *r)
{
    (void)r;
    uint8_t st = inb(0x64);
    if (!(st & 1)) return;
    uint8_t b = inb(0x60);
    if (!(st & 0x20)) return;           /* not from the aux port */

    if (cycle == 0 && !(b & 0x08)) return;  /* resync on bad first byte */
    packet[cycle++] = b;
    if (cycle < 3) return;
    cycle = 0;

    if (packet[0] & 0xC0) return;       /* overflow: discard */
    int dx = packet[1] - ((packet[0] & 0x10) ? 256 : 0);
    int dy = packet[2] - ((packet[0] & 0x20) ? 256 : 0);

    mx += dx;
    my -= dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > max_x) mx = max_x;
    if (my > max_y) my = max_y;
    mbuttons = packet[0] & 0x07;
    changed = 1;
}

void mouse_init(int screen_w, int screen_h)
{
    max_x = screen_w - 1;
    max_y = screen_h - 1;
    mx = screen_w / 2;
    my = screen_h / 2;

    wait_write(); outb(0x64, 0xA8);     /* enable aux device */
    wait_write(); outb(0x64, 0x20);     /* read controller config */
    wait_read();
    uint8_t cfg = inb(0x60);
    cfg |= 0x02;                        /* enable IRQ12 */
    cfg &= ~0x20;                       /* enable mouse clock */
    wait_write(); outb(0x64, 0x60);
    wait_write(); outb(0x60, cfg);

    mouse_write(0xF6);                  /* defaults */
    mouse_write(0xF4);                  /* start streaming */
    irq_install(12, mouse_irq);
}

bool mouse_poll(struct mouse_state *st)
{
    cli();
    int c = changed;
    changed = 0;
    st->x = mx;
    st->y = my;
    st->buttons = mbuttons;
    sti();
    return c;
}
