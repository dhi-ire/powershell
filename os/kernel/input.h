#ifndef INPUT_H
#define INPUT_H

#include "types.h"

/* Special key codes (above the ASCII range). */
enum {
    KEY_UP = 0x100, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_DEL, KEY_INS,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_SUPER,
};

#define KMOD_SHIFT 1
#define KMOD_CTRL  2
#define KMOD_ALT   4

struct key_event { int key; int mods; };

void keyboard_init(void);
bool keyboard_poll(struct key_event *ev);

struct mouse_state { int x, y; int buttons; };  /* bit0 left, bit1 right, bit2 middle */

void mouse_init(int screen_w, int screen_h);
bool mouse_poll(struct mouse_state *st);        /* true if something changed */

#endif
