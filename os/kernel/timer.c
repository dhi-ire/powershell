/* 8253/8254 PIT: system tick counter. */
#include "cpu.h"
#include "io.h"

static volatile uint32_t ticks;
static uint32_t tick_hz = 100;

static void timer_irq(struct regs *r) { (void)r; ticks++; }

void timer_init(uint32_t hz)
{
    tick_hz = hz;
    uint32_t div = 1193182 / hz;
    outb(0x43, 0x36);
    outb(0x40, div & 0xFF);
    outb(0x40, div >> 8);
    irq_install(0, timer_irq);
}

uint32_t timer_ticks(void) { return ticks; }

void sleep_ms(uint32_t ms)
{
    uint32_t end = ticks + (ms * tick_hz + 999) / 1000;
    while ((int32_t)(end - ticks) > 0) hlt();
}
