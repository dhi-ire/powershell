/* GDT, IDT and 8259 PIC setup, plus the common interrupt dispatcher. */
#include "cpu.h"
#include "io.h"
#include "lib.h"
#include "gfx.h"

struct gdt_entry { uint16_t limit_lo, base_lo; uint8_t base_mid, access, gran, base_hi; } __attribute__((packed));
struct idt_entry { uint16_t off_lo, sel; uint8_t zero, flags; uint16_t off_hi; } __attribute__((packed));
struct dt_ptr    { uint16_t limit; uint32_t base; } __attribute__((packed));

static struct gdt_entry gdt[3];
static struct idt_entry idt[256];
static irq_handler_t irq_handlers[16];

extern void gdt_flush(struct dt_ptr *);
extern void idt_load(struct dt_ptr *);
extern uint32_t isr_table[48];

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[i].base_lo = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].base_hi = base >> 24;
    gdt[i].limit_lo = limit & 0xFFFF;
    gdt[i].gran = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access = access;
}

static void idt_set(int i, uint32_t off)
{
    idt[i].off_lo = off & 0xFFFF;
    idt[i].off_hi = off >> 16;
    idt[i].sel = 0x08;
    idt[i].zero = 0;
    idt[i].flags = 0x8E;   /* present, ring 0, 32-bit interrupt gate */
}

static void pic_remap(void)
{
    uint8_t m1 = inb(0x21), m2 = inb(0xA1);
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();   /* master -> vectors 32-39 */
    outb(0xA1, 0x28); io_wait();   /* slave  -> vectors 40-47 */
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    (void)m1; (void)m2;
    outb(0x21, 0xFF);              /* mask everything until drivers register */
    outb(0xA1, 0xFF);
}

void cpu_init(void)
{
    struct dt_ptr gp = { sizeof(gdt) - 1, (uint32_t)gdt };
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);   /* kernel code */
    gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xCF);   /* kernel data */
    gdt_flush(&gp);

    memset(idt, 0, sizeof(idt));
    for (int i = 0; i < 48; i++) idt_set(i, isr_table[i]);
    struct dt_ptr ip = { sizeof(idt) - 1, (uint32_t)idt };
    idt_load(&ip);

    pic_remap();
}

void irq_install(int irq, irq_handler_t h)
{
    irq_handlers[irq] = h;
    if (irq < 8) {
        outb(0x21, inb(0x21) & ~(1 << irq));
    } else {
        outb(0x21, inb(0x21) & ~(1 << 2));    /* cascade */
        outb(0xA1, inb(0xA1) & ~(1 << (irq - 8)));
    }
}

static const char *exc_names[32] = {
    "Divide by zero", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range",
    "Invalid opcode", "Device not available", "Double fault", "Coprocessor overrun",
    "Invalid TSS", "Segment not present", "Stack fault", "General protection fault",
    "Page fault", "Reserved", "x87 FPU error", "Alignment check", "Machine check",
    "SIMD error", "Virtualization", "Control protection",
};

void panic(const char *msg)
{
    cli();
    gfx_panic_screen(msg);
    for (;;) hlt();
}

void isr_handler(struct regs *r)
{
    if (r->vector < 32) {
        char buf[160];
        const char *name = exc_names[r->vector] ? exc_names[r->vector] : "Unknown exception";
        snprintf(buf, sizeof(buf), "%s (vector %u, error %x) at EIP=%08x",
                 name, r->vector, r->err, r->eip);
        panic(buf);
    }
    int irq = r->vector - 32;
    if (irq_handlers[irq]) irq_handlers[irq](r);
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void sys_reboot(void)
{
    cli();
    while (inb(0x64) & 0x02) ;
    outb(0x64, 0xFE);                 /* pulse CPU reset line */
    for (;;) hlt();
}

void sys_shutdown(void)
{
    outw(0x604, 0x2000);              /* QEMU (q35/newer) */
    outw(0xB004, 0x2000);             /* Bochs / older QEMU */
    outw(0x4004, 0x3400);             /* VirtualBox */
    panic("It is now safe to turn off your computer.");
}
