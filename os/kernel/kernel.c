/* NovaOS kernel entry point. */
#include "types.h"
#include "cpu.h"
#include "gfx.h"
#include "heap.h"
#include "input.h"
#include "fs.h"
#include "wm.h"
#include "lib.h"
#include "sysinfo.h"
#include "io.h"

uint32_t sys_mem_kb;
extern uint8_t kernel_end[];

void cpu_brand(char *buf, size_t n)
{
    uint32_t r[12];
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(0x80000000));
    if (r[0] < 0x80000004) {
        uint32_t v[3];
        __asm__ volatile("cpuid" : "=b"(v[0]), "=d"(v[1]), "=c"(v[2]) : "a"(0));
        char vendor[13];
        memcpy(vendor, v, 12);
        vendor[12] = 0;
        strlcpy(buf, vendor, n);
        return;
    }
    for (int i = 0; i < 3; i++)
        __asm__ volatile("cpuid" : "=a"(r[i * 4]), "=b"(r[i * 4 + 1]), "=c"(r[i * 4 + 2]), "=d"(r[i * 4 + 3])
                         : "a"(0x80000002 + i));
    char tmp[49];
    memcpy(tmp, r, 48);
    tmp[48] = 0;
    char *s = tmp;
    while (*s == ' ') s++;
    strlcpy(buf, s, n);
}

void uptime_str(char *buf, size_t n)
{
    uint32_t s = timer_ticks() / 100;
    uint32_t h = s / 3600, m = (s / 60) % 60;
    if (h) snprintf(buf, n, "%u hours, %u mins", h, m);
    else if (m) snprintf(buf, n, "%u mins, %u secs", m, s % 60);
    else snprintf(buf, n, "%u secs", s);
}

static void boot_splash(void)
{
    gfx_fill(&screen, 0, 0, screen.w, screen.h, RGB(12, 14, 24));
    int cx = screen.w / 2, cy = screen.h / 2 - 40;
    gfx_fill_circle(&screen, cx, cy, 44, RGB(120, 220, 200));
    gfx_fill_circle(&screen, cx, cy, 20, RGB(12, 14, 24));
    const char *t = "NovaOS";
    gfx_text_bold(&screen, cx - gfx_text_width(t) / 2, cy + 70, t, 0xFFFFFF);
    gfx_rect(&screen, cx - 100, cy + 110, 200, 8, RGB(60, 66, 90));
    gfx_present(0, 0, screen.w, screen.h);
    for (int i = 0; i <= 196; i += 14) {
        gfx_fill(&screen, cx - 98, cy + 112, i, 4, RGB(120, 220, 200));
        gfx_present(cx - 100, cy + 110, 200, 8);
        sleep_ms(40);
    }
}

void kmain(uint32_t magic, uint32_t *mbi)
{
    cpu_init();

    /* memory: multiboot mem_upper is KB above 1 MB */
    uint32_t mem_end = 64 * 1024 * 1024;
    if (magic == 0x2BADB002 && (mbi[0] & 1)) {
        sys_mem_kb = mbi[2] + 1024;
        mem_end = (mbi[2] + 1024) * 1024;
    } else {
        sys_mem_kb = 64 * 1024;
    }
    if (mem_end > 0xE0000000u) mem_end = 0xE0000000u;
    /* keep clear of the multiboot info / GRUB modules just after the kernel */
    uint32_t heap_start = ((uint32_t)kernel_end + 0x10000) & ~0xFFFu;
    if (mem_end < heap_start + 16 * 1024 * 1024) panic("NovaOS needs at least 32 MB of RAM");
    heap_init(heap_start, mem_end);

    if (!gfx_init(magic, mbi)) panic("No usable graphics mode (need 32bpp linear framebuffer)");

    timer_init(100);
    keyboard_init();
    mouse_init(screen.w, screen.h);
    sti();

    boot_splash();
    fs_init();
    wm_init();
    app_terminal();
    wm_run();
}
