#ifndef CPU_H
#define CPU_H

#include "types.h"

struct regs {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t vector, err;
    uint32_t eip, cs, eflags;
};

typedef void (*irq_handler_t)(struct regs *r);

void cpu_init(void);                          /* GDT, IDT, PIC */
void irq_install(int irq, irq_handler_t h);   /* irq 0-15 */
void panic(const char *msg);

/* timer.c */
void timer_init(uint32_t hz);
uint32_t timer_ticks(void);                   /* 100 ticks per second */
void sleep_ms(uint32_t ms);

/* power */
void sys_reboot(void);
void sys_shutdown(void);

#endif
