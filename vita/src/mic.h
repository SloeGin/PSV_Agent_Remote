#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The microphone, 16 kHz mono 16-bit. The port is open only while recording:
 * mic_start opens it and starts a thread filling a one-second buffer, which the
 * main loop drains with mic_read. */
bool mic_start(void);
int mic_read(int16_t *out, int max); /* samples copied */
int mic_level(void);                 /* 0-100, the last grain's peak */
void mic_stop(void);
