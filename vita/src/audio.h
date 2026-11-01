#pragma once

#include <stdbool.h>

typedef enum { SOUND_REQUEST, SOUND_IDLE } Sound;

void audio_init(void);
void audio_play(Sound s);
/* While held (recording a voice reply), sounds wait: the latest plays on release. */
void audio_hold(bool hold);
