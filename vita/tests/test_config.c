#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "test.h"

static void t_defaults(void) {
    AppConfig c;
    config_defaults(&c);
    CHECK(c.n_brokers == 1);
    CHECK_STR(c.brokers[0].host, "192.168.2.222");
    CHECK(c.brokers[0].port == 1883);
    CHECK(c.active == 0 && c.sound == 1 && c.dim_after_s == 300 && c.front_touch == 1);
    CHECK(c.mode == MODE_SOFA);
}

static void t_broker_parse(void) {
    Broker b;
    CHECK(broker_parse(" 10.0.0.5 ", &b) && b.port == 1883);
    CHECK_STR(b.host, "10.0.0.5");
    CHECK(broker_parse("10.0.0.5:1884", &b) && b.port == 1884);
    CHECK(!broker_parse("10.0.0.256", &b));
    CHECK(!broker_parse("10.0.0", &b));
    CHECK(!broker_parse("10.0.0.5:0", &b));
    CHECK(!broker_parse("10.0.0.5:99999", &b));
    CHECK(!broker_parse("10.0.0.5:", &b));
    CHECK(!broker_parse("pop-os.local", &b));
    CHECK(!broker_parse("", &b));
}

static void t_parse(void) {
    AppConfig c;
    config_defaults(&c);
    config_parse(&c, "# x\n broker = 10.0.0.5 \nbroker=bad\nbroker=172.16.0.2:1884\nactive=9\nmode=desk\nfoo=1\n");
    CHECK(c.n_brokers == 2);
    CHECK_STR(c.brokers[0].host, "10.0.0.5");
    CHECK(c.brokers[1].port == 1884);
    CHECK(c.active == 0);
    CHECK(c.mode == MODE_DESK);
}

static void t_parse_bad_values_keep_old(void) {
    AppConfig c;
    config_defaults(&c);
    config_parse(&c, "sound=abc\ndim_after_s=-5\nmode=couch\nbroker=nope\n");
    CHECK(c.sound == 1 && c.dim_after_s == 300 && c.mode == MODE_SOFA);
    CHECK(c.n_brokers == 1);
}

static void t_add_remove(void) {
    AppConfig c;
    config_defaults(&c);
    Broker b;
    broker_parse("10.0.0.5", &b);
    CHECK(config_add_broker(&c, &b) == BROKER_ADDED);
    CHECK(config_add_broker(&c, &b) == BROKER_EXISTS); /* 重复 */
    c.active = 1;
    CHECK(config_remove_broker(&c, 0));
    CHECK(c.active == 0);
    CHECK_STR(c.brokers[0].host, "10.0.0.5");
    CHECK(!config_remove_broker(&c, 0)); /* 至少保留一个 */
}

static void t_add_full(void) {
    AppConfig c;
    config_defaults(&c);
    char s[32];
    for (int i = 1; i < MAX_BROKERS; i++) {
        Broker b;
        snprintf(s, sizeof s, "10.0.0.%d", i);
        broker_parse(s, &b);
        CHECK(config_add_broker(&c, &b) == BROKER_ADDED);
    }
    Broker extra;
    broker_parse("10.0.1.1", &extra);
    CHECK(config_add_broker(&c, &extra) == BROKER_FULL);
    CHECK(c.n_brokers == MAX_BROKERS);
}

static void t_roundtrip(void) {
    AppConfig a, b;
    config_defaults(&a);
    a.mode = MODE_DESK;
    a.sound = 0;
    a.front_touch = 0;
    Broker x;
    broker_parse("10.1.2.3:1884", &x);
    config_add_broker(&a, &x);
    a.active = 1;
    char buf[512];
    CHECK(config_serialize(&a, buf, sizeof buf) > 0);
    config_defaults(&b);
    config_parse(&b, buf);
    CHECK(b.mode == MODE_DESK && b.sound == 0 && b.n_brokers == 2 && b.active == 1);
    CHECK(b.front_touch == 0);
    CHECK(b.brokers[1].port == 1884);
}

static void t_parse_tls(void) {
    Broker b;
    CHECK(broker_parse("tls://10.0.0.5", &b) && b.tls && b.port == 8883);
    CHECK(broker_parse("tls://10.0.0.5:9000", &b) && b.tls && b.port == 9000);
    CHECK(broker_parse("10.0.0.5", &b) && !b.tls && b.port == 1883);
    CHECK(!broker_parse("tls://", &b));
}

static void t_tls_roundtrip(void) {
    AppConfig a, b;
    config_defaults(&a);
    Broker x;
    CHECK(broker_parse("tls://10.0.0.5:8883", &x));
    CHECK(config_add_broker(&a, &x) == BROKER_ADDED);
    char buf[512];
    CHECK(config_serialize(&a, buf, sizeof buf) > 0);
    CHECK(strstr(buf, "broker=tls://10.0.0.5:8883\n") != NULL);
    config_defaults(&b);
    config_parse(&b, buf);
    CHECK(b.n_brokers == 2 && b.brokers[1].tls && b.brokers[1].port == 8883 && !b.brokers[0].tls);
}

static void t_plain_roundtrip_unchanged(void) {
    AppConfig a;
    config_defaults(&a);
    char buf[512];
    CHECK(config_serialize(&a, buf, sizeof buf) > 0);
    CHECK_STR(buf, "# PSV Agent Remote\nbroker=192.168.2.222:1883\nactive=0\nsound=1\nfront_touch=1\n"
                   "dim_after_s=300\nmode=sofa\n");
}

static void t_add_replaces_kind(void) {
    AppConfig a;
    config_defaults(&a);
    Broker plain, tls;
    broker_parse("10.0.0.5:8883", &plain);
    broker_parse("tls://10.0.0.5:8883", &tls);
    CHECK(config_add_broker(&a, &plain) == BROKER_ADDED);
    CHECK(config_add_broker(&a, &tls) == BROKER_REPLACED);
    CHECK(a.n_brokers == 2 && a.brokers[1].tls);
    CHECK(config_add_broker(&a, &tls) == BROKER_EXISTS);
}

static void broker_list(Broker *b) {
    broker_parse("10.0.0.1", &b[0]);
    broker_parse("tls://10.0.0.2", &b[1]);
    broker_parse("10.0.0.3", &b[2]);
    broker_parse("tls://10.0.0.4", &b[3]);
}

static void t_bad_broker_line_no_plain_default(void) {
    AppConfig c;
    config_defaults(&c);
    config_parse(&c, "broker=TLS://192.168.1.20\n");
    CHECK(c.n_brokers == 1 && c.brokers[0].tls && !strcmp(c.brokers[0].host, "192.168.1.20"));
    config_defaults(&c);
    config_parse(&c, "broker=tls://mybox.local\n");
    CHECK(c.n_brokers == 0); /* never the plaintext default in its place */
}

static void t_order_tls_active_skips_plain(void) {
    Broker b[4];
    int order[4];
    broker_list(b);
    CHECK(broker_try_order(b, 4, 1, order) == 2);
    CHECK(order[0] == 1 && order[1] == 3);
}

static void t_order_plain_active_tries_all(void) {
    Broker b[4];
    int order[4];
    broker_list(b);
    CHECK(broker_try_order(b, 4, 2, order) == 4);
    CHECK(order[0] == 2 && order[1] == 3 && order[2] == 0 && order[3] == 1);
}

static void t_migrate_data_dir(void) {
    char base[] = "/tmp/psv_migrate_XXXXXX";
    CHECK(mkdtemp(base) != NULL);
    char old_dir[96], new_dir[96], f[128];
    snprintf(old_dir, sizeof old_dir, "%s/old", base);
    snprintf(new_dir, sizeof new_dir, "%s/new", base);
    CHECK(mkdir(old_dir, 0700) == 0);
    snprintf(f, sizeof f, "%s/psk.txt", old_dir);
    FILE *fp = fopen(f, "w");
    CHECK(fp != NULL);
    fputs("psv:00\n", fp);
    fclose(fp);
    CHECK(config_migrate_dir(old_dir, new_dir));       /* old moved to new */
    snprintf(f, sizeof f, "%s/psk.txt", new_dir);
    CHECK(access(f, F_OK) == 0 && access(old_dir, F_OK) != 0);
    CHECK(!config_migrate_dir(old_dir, new_dir));      /* nothing left to move */
    CHECK(mkdir(old_dir, 0700) == 0);
    CHECK(!config_migrate_dir(old_dir, new_dir));      /* new exists: never overwritten */
    CHECK(access(f, F_OK) == 0);
    remove(f);
    rmdir(new_dir);
    rmdir(old_dir);
    rmdir(base);
}

int main(void) {
    RUN(t_defaults);
    RUN(t_migrate_data_dir);
    RUN(t_parse_tls);
    RUN(t_tls_roundtrip);
    RUN(t_plain_roundtrip_unchanged);
    RUN(t_add_replaces_kind);
    RUN(t_bad_broker_line_no_plain_default);
    RUN(t_order_tls_active_skips_plain);
    RUN(t_order_plain_active_tries_all);
    RUN(t_broker_parse);
    RUN(t_parse);
    RUN(t_parse_bad_values_keep_old);
    RUN(t_add_remove);
    RUN(t_add_full);
    RUN(t_roundtrip);
    return TEST_RESULT();
}
