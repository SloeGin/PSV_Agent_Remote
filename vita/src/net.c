#include "net.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mqtt_client.h"
#include "outbox.h"
#include "protocol.h"
#include "psk.h"
#include "reconnect.h"

#define INBOX_CAP 256
#define NET_MEM_SIZE (1 * 1024 * 1024)
#define CONNECT_KEEPALIVE_S 30

#define TOPIC_ONLINE "claude/psv/online"
#define TOPIC_MODE "claude/psv/mode"

static SceKernelLwMutexWork g_lock;
static InMsg g_inbox[INBOX_CAP];
static int g_in_head, g_in_count;
static Outbox g_outbox;

static Broker g_brokers[MAX_BROKERS];
static int g_n_brokers, g_active;
static volatile int g_generation;
static volatile Mode g_mode;
static volatile int g_mode_dirty;
static volatile int g_online_dirty;
static volatile NetStatus g_status = NET_DOWN;
static volatile int g_connected_idx = -1;
static volatile NetError g_errors[MAX_BROKERS];
static volatile int g_running;
static SceUID g_thread = -1;
static uint8_t g_net_mem[NET_MEM_SIZE];

static void lock(void) { sceKernelLockLwMutex(&g_lock, 1, NULL); }
static void unlock(void) { sceKernelUnlockLwMutex(&g_lock, 1); }

static void on_message(const char *topic, const uint8_t *payload, size_t len, void *ud) {
    (void)ud;
    if (protocol_is_offline_flag(topic, payload, len)) {
        g_online_dirty = 1; /* a stale will overwrote us: say online again */
        return;
    }
    uint8_t *copy = malloc(len + 1);
    if (!copy) return;
    memcpy(copy, payload, len);
    copy[len] = '\0';
    lock();
    if (g_in_count == INBOX_CAP) { /* drop the oldest */
        free(g_inbox[g_in_head].payload);
        g_in_head = (g_in_head + 1) % INBOX_CAP;
        g_in_count--;
    }
    InMsg *m = &g_inbox[(g_in_head + g_in_count) % INBOX_CAP];
    snprintf(m->topic, sizeof m->topic, "%s", topic);
    m->payload = copy;
    m->len = len;
    g_in_count++;
    unlock();
}

bool net_pop(InMsg *out) {
    lock();
    bool ok = g_in_count > 0;
    if (ok) {
        *out = g_inbox[g_in_head];
        g_in_head = (g_in_head + 1) % INBOX_CAP;
        g_in_count--;
    }
    unlock();
    return ok;
}

void net_publish(const char *topic, const void *payload, size_t len, int qos, int retain) {
    lock();
    outbox_push(&g_outbox, topic, payload, len, qos, retain);
    unlock();
}

/* Send what is queued and drop what the broker acknowledged; false if the connection failed.
 * The payload is copied out so the main thread can keep queueing while we send. */
static bool flush_outbox(MqttClient *c) {
    lock();
    for (int i = g_outbox.count - 1; i >= 0; i--) {
        OutMsg *m = &g_outbox.q[i];
        if (m->sent && mqtt_acked(c, m->packet_id)) outbox_ack(&g_outbox, m->packet_id);
    }
    unlock();
    for (;;) {
        lock();
        const OutMsg *next = outbox_next_unsent(&g_outbox);
        OutMsg m;
        uint8_t *copy = NULL;
        if (next) {
            m = *next;
            copy = malloc(m.len ? m.len : 1);
            if (copy) memcpy(copy, m.payload, m.len);
        }
        unlock();
        if (!next) return true;
        if (!copy) return true; /* out of memory: try again next poll */
        int id = mqtt_publish(c, m.topic, copy, m.len, m.qos, m.retain);
        free(copy);
        if (id < 0) return false; /* stays queued; resent after reconnect */
        lock();
        outbox_mark_sent(&g_outbox, m.seq, (unsigned short)id);
        unlock();
    }
}

static const char *mode_str(Mode m) { return m == MODE_DESK ? "desk" : "sofa"; }

static bool wifi_up(void) {
    int state = 0;
    return sceNetCtlInetGetState(&state) >= 0 && state == SCE_NETCTL_STATE_CONNECTED;
}

/* Sleep up to ms, waking early when stopping or the broker list changes. */
static void nap(int ms, int generation) {
    for (int t = 0; t < ms && g_running && g_generation == generation; t += 100) sceKernelDelayThread(100 * 1000);
}

static int net_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    char client_id[16];
    snprintf(client_id, sizeof client_id, "psv-%06x", (unsigned)(sceKernelGetProcessTimeWide() & 0xFFFFFF));
    Reconnect policy;
    reconnect_init(&policy);
    while (g_running) {
        int generation = g_generation;
        if (!wifi_up()) {
            g_status = NET_DOWN;
            nap(500, generation);
            continue;
        }
        lock();
        Broker brokers[MAX_BROKERS];
        int n = g_n_brokers, start = g_active;
        memcpy(brokers, g_brokers, sizeof brokers);
        unlock();

        MqttClient *c = NULL;
        int idx = -1;
        g_status = NET_CONNECTING;
        /* Re-read the key every round, so a newly copied psk.txt works without a restart. */
        Psk psk;
        bool have_psk = psk_load(PSK_PATH, &psk);
        int order[MAX_BROKERS];
        int tries = broker_try_order(brokers, n, start, order);
        for (int k = 0; k < tries && g_running && g_generation == generation; k++) {
            int i = order[k];
            if (brokers[i].tls && !have_psk) {
                g_errors[i] = NETERR_NO_KEY;
                continue;
            }
            c = brokers[i].tls ? mqtt_create_tls(brokers[i].host, brokers[i].port, client_id, &psk)
                               : mqtt_create(brokers[i].host, brokers[i].port, client_id);
            if (c && mqtt_connect(c, TOPIC_ONLINE, "0", 1, CONNECT_KEEPALIVE_S) == 0) {
                g_errors[i] = NETERR_NONE;
                idx = i;
                break;
            }
            MqttError e = c ? mqtt_last_error(c) : MQTT_ERR_TLS;
            g_errors[i] = e == MQTT_ERR_KEY ? NETERR_KEY : e == MQTT_ERR_TLS ? NETERR_TLS : NETERR_UNREACHABLE;
            mqtt_destroy(c);
            c = NULL;
        }
        psk_wipe(&psk);
        if (!c) {
            g_status = NET_DOWN;
            nap(reconnect_after_failure(&policy) * 1000, generation);
            continue;
        }

        uint64_t session_start = sceKernelGetProcessTimeWide() / 1000;
        g_connected_idx = idx;
        lock();
        outbox_requeue_all(&g_outbox); /* anything unacknowledged goes out again */
        unlock();
        g_mode_dirty = 0;
        const char *mode = mode_str(g_mode);
        bool ok = mqtt_publish(c, TOPIC_ONLINE, "1", 1, 1, 1) >= 0;
        ok = ok && mqtt_publish(c, TOPIC_MODE, mode, strlen(mode), 1, 1) >= 0;
        ok = ok && mqtt_subscribe(c, "claude/session/#", 0) == 0;
        ok = ok && mqtt_subscribe(c, TOPIC_ONLINE, 0) == 0;
        g_online_dirty = 0;
        if (ok) g_status = NET_UP;
        while (ok && g_running && g_generation == generation) {
            if (g_online_dirty) {
                g_online_dirty = 0;
                ok = mqtt_publish(c, TOPIC_ONLINE, "1", 1, 1, 1) >= 0;
            }
            if (g_mode_dirty) {
                g_mode_dirty = 0;
                mode = mode_str(g_mode);
                ok = mqtt_publish(c, TOPIC_MODE, mode, strlen(mode), 1, 1) >= 0;
            }
            ok = ok && mqtt_poll(c, 50, on_message, NULL) >= 0 && flush_outbox(c);
        }
        if (ok && !g_running) {
            /* A clean DISCONNECT suppresses the will, so say offline ourselves. */
            mqtt_publish(c, TOPIC_ONLINE, "0", 1, 1, 1);
            mqtt_poll(c, 100, NULL, NULL);
        }
        mqtt_destroy(c);
        g_connected_idx = -1;
        g_status = NET_DOWN;
        if (g_running && g_generation == generation) {
            uint64_t lasted = sceKernelGetProcessTimeWide() / 1000 - session_start;
            nap(reconnect_after_session(&policy, lasted) * 1000, generation);
        }
    }
    return sceKernelExitDeleteThread(0);
}

static void copy_brokers(const AppConfig *cfg) {
    lock();
    memcpy(g_brokers, cfg->brokers, sizeof g_brokers);
    g_n_brokers = cfg->n_brokers;
    g_active = cfg->active < cfg->n_brokers ? cfg->active : 0;
    unlock();
}

void net_start(const AppConfig *cfg, Mode initial_mode) {
    sceKernelCreateLwMutex(&g_lock, "net_lock", 0, 0, NULL);
    outbox_init(&g_outbox);
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    SceNetInitParam param = {g_net_mem, sizeof g_net_mem, 0};
    sceNetInit(&param);
    sceNetCtlInit();
    copy_brokers(cfg);
    g_mode = initial_mode;
    g_running = 1;
    g_thread = sceKernelCreateThread("net", net_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (g_thread >= 0) sceKernelStartThread(g_thread, 0, NULL);
}

void net_stop(void) {
    g_running = 0;
    if (g_thread >= 0) sceKernelWaitThreadEnd(g_thread, NULL, NULL);
    g_thread = -1;
}

void net_set_brokers(const AppConfig *cfg) {
    copy_brokers(cfg);
    for (int i = 0; i < MAX_BROKERS; i++) g_errors[i] = NETERR_NONE;
    g_generation++;
}

void net_set_mode(Mode mode) {
    g_mode = mode;
    g_mode_dirty = 1;
}

NetStatus net_status(void) { return g_status; }
int net_connected_index(void) { return g_connected_idx; }
NetError net_broker_error(int idx) { return idx >= 0 && idx < MAX_BROKERS ? g_errors[idx] : NETERR_NONE; }
