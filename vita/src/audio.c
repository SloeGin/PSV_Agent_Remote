#include "audio.h"

#include <math.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#define RATE 48000
#define GRAIN 256
#define AMPLITUDE 6000

typedef struct {
    float hz;
    int ms;
} Tone;

/* hz == 0 is silence. */
static const Tone REQUEST_TONES[] = {{880, 120}, {0, 80}, {880, 120}};
static const Tone IDLE_TONES[] = {{660, 150}, {990, 150}};

static int g_port = -1;
static SceUID g_sema = -1;
static volatile Sound g_pending;

static void play(const Tone *tones, int n) {
    static short buf[GRAIN];
    float phase = 0;
    for (int t = 0; t < n; t++) {
        int samples = RATE * tones[t].ms / 1000;
        for (int done = 0; done < samples; done += GRAIN) {
            for (int i = 0; i < GRAIN; i++) {
                int k = done + i;
                /* short fade in/out avoids clicks */
                float env = k < 240 ? k / 240.0f : (samples - k < 240 ? (samples - k) / 240.0f : 1.0f);
                if (env < 0) env = 0;
                buf[i] = tones[t].hz > 0 ? (short)(AMPLITUDE * env * sinf(phase)) : 0;
                phase += 2 * (float)M_PI * tones[t].hz / RATE;
            }
            sceAudioOutOutput(g_port, buf);
        }
    }
}

static int audio_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (;;) {
        sceKernelWaitSema(g_sema, 1, NULL);
        if (g_pending == SOUND_REQUEST) play(REQUEST_TONES, 3);
        else play(IDLE_TONES, 2);
    }
    return 0;
}

void audio_init(void) {
    g_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, GRAIN, RATE, SCE_AUDIO_OUT_MODE_MONO);
    if (g_port < 0) return;
    int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(g_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
    g_sema = sceKernelCreateSema("audio", 0, 0, 1, NULL);
    SceUID th = sceKernelCreateThread("audio", audio_thread, 0x10000100, 0x4000, 0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
}

void audio_play(Sound s) {
    if (g_sema < 0) return;
    g_pending = s;
    sceKernelSignalSema(g_sema, 1);
}
