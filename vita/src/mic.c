#include "mic.h"

#include <psp2/audioin.h>
#include <psp2/kernel/threadmgr.h>

#include "voicebuf.h"

#define RATE 16000
#define GRAIN 256

/* The system-wide microphone mute (Party and other apps can set it): a muted mic
 * records silence. Missing from the SDK headers; commands 1 = mute, 2 = unmute. */
int sceAudioInSetMute(int command);
#define MIC_MUTE 1
#define MIC_UNMUTE 2

static VoiceBuf g_buf;
static int g_port = -1;
static SceUID g_thread = -1;
static volatile bool g_running;
static volatile int g_level;
static bool g_remute; /* we lifted the system mute; restore it on stop */

static int mic_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    static int16_t grain[GRAIN];
    while (g_running) {
        if (sceAudioInInput(g_port, grain) < 0) break; /* blocks until a grain is filled */
        voicebuf_push(&g_buf, grain, GRAIN);
        g_level = voicebuf_level(grain, GRAIN);
    }
    return 0;
}

bool mic_start(void) {
    if (g_port >= 0) return true;
    voicebuf_init(&g_buf);
    g_level = 0;
    g_port = sceAudioInOpenPort(SCE_AUDIO_IN_PORT_TYPE_VOICE, GRAIN, RATE, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);
    if (g_port < 0) return false;
    g_remute = sceAudioInGetStatus(SCE_AUDIO_IN_GETSTATUS_MUTE) == 1 && sceAudioInSetMute(MIC_UNMUTE) >= 0;
    g_running = true;
    g_thread = sceKernelCreateThread("mic", mic_thread, 0x10000100, 0x4000, 0, 0, NULL);
    if (g_thread < 0 || sceKernelStartThread(g_thread, 0, NULL) < 0) {
        mic_stop();
        return false;
    }
    return true;
}

int mic_read(int16_t *out, int max) {
    return voicebuf_pop(&g_buf, out, max);
}

int mic_level(void) {
    return g_level;
}

void mic_stop(void) {
    g_running = false;
    if (g_thread >= 0) {
        sceKernelWaitThreadEnd(g_thread, NULL, NULL); /* the last grain is at most 16 ms away */
        sceKernelDeleteThread(g_thread);
        g_thread = -1;
    }
    if (g_port >= 0) {
        sceAudioInReleasePort(g_port);
        g_port = -1;
    }
    if (g_remute) {
        sceAudioInSetMute(MIC_MUTE);
        g_remute = false;
    }
}
