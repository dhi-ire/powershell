/* PS/2 keyboard driver (scancode set 1, US layout). */
#include "input.h"
#include "cpu.h"
#include "io.h"

#define QSIZE 128
static struct key_event queue[QSIZE];
static volatile unsigned qhead, qtail;
static int shift, ctrl, alt, caps, extended;

static const char normal_map[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ',
};

static const char shift_map[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ',
};

static void push(int key)
{
    unsigned next = (qhead + 1) % QSIZE;
    if (next == qtail) return;   /* queue full: drop */
    queue[qhead].key = key;
    queue[qhead].mods = (shift ? KMOD_SHIFT : 0) | (ctrl ? KMOD_CTRL : 0) | (alt ? KMOD_ALT : 0);
    qhead = next;
}

static void keyboard_irq(struct regs *r)
{
    (void)r;
    if (!(inb(0x64) & 1)) return;
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) { extended = 1; return; }
    int release = sc & 0x80;
    sc &= 0x7F;

    if (extended) {
        extended = 0;
        if (sc == 0x1D) { ctrl = !release; return; }
        if (sc == 0x38) { alt = !release; return; }
        if (release) return;
        switch (sc) {
        case 0x48: push(KEY_UP); break;
        case 0x50: push(KEY_DOWN); break;
        case 0x4B: push(KEY_LEFT); break;
        case 0x4D: push(KEY_RIGHT); break;
        case 0x47: push(KEY_HOME); break;
        case 0x4F: push(KEY_END); break;
        case 0x49: push(KEY_PGUP); break;
        case 0x51: push(KEY_PGDN); break;
        case 0x53: push(KEY_DEL); break;
        case 0x52: push(KEY_INS); break;
        case 0x5B: case 0x5C: push(KEY_SUPER); break;
        case 0x1C: push('\n'); break;          /* keypad enter */
        case 0x35: push('/'); break;
        }
        return;
    }

    switch (sc) {
    case 0x2A: case 0x36: shift = !release; return;
    case 0x1D: ctrl = !release; return;
    case 0x38: alt = !release; return;
    case 0x3A: if (!release) caps = !caps; return;
    }
    if (release) return;

    if (sc >= 0x3B && sc <= 0x44) { push(KEY_F1 + (sc - 0x3B)); return; }
    if (sc == 0x57) { push(KEY_F11); return; }
    if (sc == 0x58) { push(KEY_F12); return; }

    char c = shift ? shift_map[sc] : normal_map[sc];
    if (caps && c >= 'a' && c <= 'z') c -= 32;
    else if (caps && c >= 'A' && c <= 'Z') c += 32;
    if (c) push((uint8_t)c);
}

void keyboard_init(void)
{
    while (inb(0x64) & 1) inb(0x60);   /* flush */
    irq_install(1, keyboard_irq);
}

bool keyboard_poll(struct key_event *ev)
{
    if (qtail == qhead) return false;
    *ev = queue[qtail];
    qtail = (qtail + 1) % QSIZE;
    return true;
}
