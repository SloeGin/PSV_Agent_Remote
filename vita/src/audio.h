#pragma once

typedef enum { SOUND_REQUEST, SOUND_IDLE } Sound;

void audio_init(void);
void audio_play(Sound s);
