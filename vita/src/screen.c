#include "screen.h"

void screen_init(Screen *s) {
    s->off = false;
}

bool screen_step(Screen *s, bool dimmed, bool dialog_open) {
    if (dimmed && !dialog_open) {
        if (s->off) return false;
        s->off = true;
        return true; /* leave a black frame behind */
    }
    s->off = false;
    return true;
}
