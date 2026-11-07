/* Netplay test bench, run on the headset from adb shell (no VR, no screen):
 *
 *   tremu_nettest replay <core.so> <game> [frames]
 *       Save state determinism: runs the game, saves its state, plays scripted inputs, loads the
 *       state back and plays the same inputs again. Both runs must end in the same state, or a
 *       player joining a running game would play a different game from the host.
 *   tremu_nettest host <core.so> <game> [frames]
 *   TREMU_NET_PORT=47630 tremu_nettest guest <core.so> <game> [frames]
 *       The real network path on one device: a host, then a guest joining it over the loopback.
 *       Each prints "SYNC <frame> <crc of the whole state>" every 300 lockstep frames; the two
 *       lists must match line for line.
 *
 * Inputs are scripted (aim sweeping the screen, trigger, coin and start now and then), the
 * same function of the frame number on every run. */
#include "../src/core.h"
#include "../src/netplay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

static void script(uint32_t f, int player, TremuGun *g) {
    memset(g, 0, sizeof *g);
    uint32_t k = f * 37u + (uint32_t)player * 5000u;
    g->x = (int16_t)((int)(k % 50000u) - 25000);
    g->y = (int16_t)((int)((f * 23u + (uint32_t)player * 9000u) % 40000u) - 20000);
    g->trigger = (f / 4 + (uint32_t)player) % 3 == 0;
    uint32_t t = (f + (uint32_t)player * 50u) % 1200u;
    g->select = t >= 100 && t < 106;  /* coin */
    g->start = t >= 200 && t < 206;
    g->pad = (uint16_t)(g->start ? 1u << 3 : 0) | (uint16_t)(g->select ? 1u << 2 : 0) | (uint16_t)(g->trigger ? 1u : 0);
    g->lx = (int16_t)((int)(k % 60001u) - 30000);
    g->ly = (int16_t)((int)((k * 7u) % 60001u) - 30000);
}

static uint32_t state_crc(size_t *size_out) {
    size_t size = tremu_core_state_size();
    if (size_out) *size_out = size;
    if (!size) return 0;
    void *buf = malloc(size);
    uint32_t crc = buf && tremu_core_save_state(buf, size) ? (uint32_t)crc32(0, buf, (uInt)size) : 0;
    free(buf);
    return crc;
}

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

/* CRC of the picture: what the players see. */
static uint32_t picture_crc(void) {
    unsigned w = 0, h = 0;
    const uint32_t *px = tremu_core_frame(&w, &h);
    return px && w && h ? (uint32_t)crc32(0, (const void *)px, (uInt)(w * h * 4)) : 0;
}

static void run_scripted(uint32_t from, uint32_t to, uint32_t *crcs, uint32_t *pics) {
    for (uint32_t f = from; f < to; f++) {
        TremuGun guns[TREMU_PLAYERS];
        for (int p = 0; p < TREMU_PLAYERS; p++) script(f, p, &guns[p]);
        tremu_core_run(guns);
        if (crcs && (f + 1 - from) % 60 == 0) {
            crcs[(f + 1 - from) / 60 - 1] = getenv("NETTEST_RAM") ? tremu_core_ram_crc() : state_crc(NULL);
            pics[(f + 1 - from) / 60 - 1] = picture_crc();
        }
    }
}

static int replay(uint32_t frames) {
    const uint32_t warm = 900;
    run_scripted(0, warm, NULL, NULL);
    size_t size = 0;
    state_crc(&size);
    if (!size) { printf("RESULT no save state (size 0)\n"); return 2; }
    void *saved = malloc(size);
    if (!saved || !tremu_core_save_state(saved, size)) { printf("RESULT save failed\n"); return 2; }
    uint32_t checks = frames / 60;
    /* a: the game going on (the host), b and c: started again from the saved state (guests). */
    uint32_t *ram[3], *pic[3], end[3];
    uint64_t ms = 0;
    for (int run = 0; run < 3; run++) {
        ram[run] = calloc(checks, 4);
        pic[run] = calloc(checks, 4);
        if (run && !tremu_core_load_state(saved, size)) { printf("RESULT load failed\n"); return 2; }
        uint64_t t0 = now_ns();
        run_scripted(warm, warm + checks * 60, ram[run], pic[run]);
        if (!run) ms = (now_ns() - t0) / 1000000ull;
        end[run] = state_crc(NULL);
        const char *dump = getenv("NETTEST_DUMP");  /* prefix: the end state of each run */
        if (dump) {
            char path[512];
            snprintf(path, sizeof path, "%s%d.state", dump, run);
            void *buf = malloc(size);
            FILE *out = buf && tremu_core_save_state(buf, size) ? fopen(path, "wb") : NULL;
            if (out) { fwrite(buf, 1, size, out); fclose(out); }
            free(buf);
        }
    }
    printf("state %zu bytes, %u frames in %llu ms (%.0f fps)\n", size, checks * 60, (unsigned long long)ms,
           ms ? checks * 60 * 1000.0 / ms : 0.0);
    int verdict = 0;
    for (int run = 1; run < 3; run++) {
        const char *who = run == 1 ? "loaded vs live" : "loaded vs loaded";
        const uint32_t *x = ram[run == 1 ? 0 : 1], *y = ram[run], *px = pic[run == 1 ? 0 : 1], *py = pic[run];
        uint32_t ram_at = 0, pic_at = 0;
        for (uint32_t i = 0; i < checks && (!ram_at || !pic_at); i++) {
            if (!ram_at && x[i] != y[i]) ram_at = (i + 1) * 60;
            if (!pic_at && px[i] != py[i]) pic_at = (i + 1) * 60;
        }
        uint32_t ex = end[run == 1 ? 0 : 1];
        printf("%s: picture %s%s, state %s, final state %s\n", who, pic_at ? "differs from frame +" : "same", "",
               ram_at ? "differs" : "same", ex == end[run] ? "same" : "differs");
        if (pic_at) printf("  picture first differs %u frames after the load\n", pic_at);
        if (ram_at) printf("  state first differs %u frames after the load\n", ram_at);
        if (pic_at || ram_at) verdict = verdict ? verdict : run;
        else if (ex != end[run] && !verdict) verdict = 10 + run;
    }
    if (!verdict) printf("RESULT OK: joining a running game is exact\n");
    else if (verdict == 1) printf("RESULT DIVERGES: a player joining a running game sees another game\n");
    else if (verdict == 2) printf("RESULT DIVERGES: the core is not deterministic even from the same state\n");
    else printf("RESULT OK (picture and RAM same; only the saved state's bytes differ)\n");
    return verdict == 1 || verdict == 2;
}

static int network(bool host, const char *key, uint32_t frames) {
    if (host) netplay_set_boot_start(getenv("NETTEST_BOOT") != NULL);
    uint32_t crc = netplay_file_crc(getenv("NETTEST_ROM") ? getenv("NETTEST_ROM") : "");  /* as Tremu checks it */
    const char *to = getenv("NETTEST_HOST") ? getenv("NETTEST_HOST") : "127.0.0.1";
    bool ok = host ? netplay_host(key, key, crc, false) : netplay_join(to, key, crc);
    if (!ok) { printf("RESULT cannot open the network\n"); return 2; }
    uint32_t local = 0, start = 0;
    bool started = false;
    uint64_t next = now_ns(), deadline = next + 600000000000ull;
    for (;;) {
        TremuGun mine, guns[NET_MAX_PLAYERS];
        script(local++, host ? 0 : 1, &mine);
        bool run = netplay_frame(&mine, guns);
        NetState st = netplay_state();
        if (st == NET_FAILED) { char why[256]; netplay_status(why, sizeof why); printf("RESULT failed: %s\n", why); return 2; }
        if (run) {
            uint32_t f = netplay_frame_index();  /* frame f-1 runs now */
            tremu_core_run(guns);
            netplay_after_frame();
            if (st == NET_PLAYING && netplay_players() > 1) {
                if (!started) { started = true; start = f; printf("lockstep from frame %u\n", f); }
                if (f % 300 == 0) { printf("SYNC %u state %08x picture %08x ram %08x\n", f, state_crc(NULL), picture_crc(), tremu_core_ram_crc()); fflush(stdout); }
                if (f >= start + frames) {
                    const char *dump = getenv("NETTEST_DUMP");  /* state at the last frame, to diff */
                    size_t size = tremu_core_state_size();
                    void *buf = dump && size ? malloc(size) : NULL;
                    FILE *out = buf && tremu_core_save_state(buf, size) ? fopen(dump, "wb") : NULL;
                    if (out) { fwrite(buf, 1, size, out); fclose(out); }
                    free(buf);
                    break;
                }
            }
        }
        if (now_ns() > deadline) { printf("RESULT timeout\n"); return 2; }
        /* 60 Hz, like the headset (the host alone would otherwise rush ahead). */
        next += 16666667ull;
        uint64_t t = now_ns();
        if (next > t) {
            struct timespec until = {(time_t)(next / 1000000000ull), (long)(next % 1000000000ull)};
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &until, NULL);
        } else if (t - next > 100000000ull) {
            next = t;
        }
    }
    char status[256];
    netplay_status(status, sizeof status);
    printf("RESULT done: %s\n", status);
    /* Let the other side reach its last frame before saying goodbye. */
    for (int i = 0; i < 200; i++) { netplay_idle(); struct timespec s = {0, 10000000}; nanosleep(&s, NULL); }
    netplay_stop();
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s replay|host|guest <core.so> <game> [frames]\n", argv[0]);
        return 2;
    }
    uint32_t frames = argc > 4 ? (uint32_t)atoi(argv[4]) : 3600;
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *sys = getenv("TREMU_SYSTEM_DIR"), *save = getenv("TREMU_SAVE_DIR");
    if (!tremu_core_load(argv[2], sys ? sys : "/storage/emulated/0/Tremu/bios", save ? save : "/data/local/tmp/nettest/saves", argv[3])) {
        printf("RESULT cannot load %s\n", argv[3]);
        return 2;
    }
    const char *slash = getenv("NETTEST_KEY") ? NULL : strrchr(argv[3], '/');
    if (getenv("NETTEST_KEY")) argv[3] = getenv("NETTEST_KEY");  /* the key Tremu uses for this game */
    int r;
    if (!strcmp(argv[1], "boot")) {
        /* Run twice in two processes and compare: same game from power-on with the same inputs. */
        for (uint32_t f = 0; f < frames; f++) {
            TremuGun guns[TREMU_PLAYERS];
            for (int p = 0; p < TREMU_PLAYERS; p++) script(f, p, &guns[p]);
            tremu_core_run(guns);
            if ((f + 1) % 300 == 0) printf("SYNC %u %08x\n", f + 1, state_crc(NULL));
        }
        const char *dump = getenv("NETTEST_DUMP");  /* the final state, to compare byte by byte */
        size_t size = tremu_core_state_size();
        void *buf = dump && size ? malloc(size) : NULL;
        FILE *out = buf && tremu_core_save_state(buf, size) ? fopen(dump, "wb") : NULL;
        if (out) { fwrite(buf, 1, size, out); fclose(out); }
        r = 0;
    } else {
        r = !strcmp(argv[1], "replay") ? replay(frames) : network(!strcmp(argv[1], "host"), slash ? slash + 1 : argv[3], frames);
    }
    fflush(stdout);
    _Exit(r);  /* some cores hang in their teardown */
}
