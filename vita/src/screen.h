#pragma once

#include <stdbool.h>

/* Whether the frame loop draws. Dimmed, the app draws one black frame and then
 * stops rendering until it wakes; an open system dialog (IME) is drawn by our
 * loop, so it keeps rendering. */
typedef struct {
    bool off;
} Screen;

void screen_init(Screen *s);

/* True when this frame should be rendered and swapped. */
bool screen_step(Screen *s, bool dimmed, bool dialog_open);
