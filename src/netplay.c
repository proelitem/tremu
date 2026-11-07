#include "lang.h"
#include "netplay.h"
#include "internet.h"
#include <android/log.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

/* Also written to a file (netplay_set_log_file): each headset keeps the story of its last games,
 * read later with the headset plugged in, one at a time. */
static FILE *log_file;
static void net_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define LOG(...) net_log(__VA_ARGS__)

/* Input delay bounds (frames), frames repeated in every input packet, ring size. A chunk and
 * its 12-byte header fit one Wi-Fi frame (MTU 1500 less the UDP/IP headers). */
enum { DELAY_DEFAULT = 2, DELAY_MAX = 8, REDUNDANT = 10, RING = 256, CHUNK = 1400, CRC_EVERY = 60, CRC_HISTORY = 8,
       SETTLE_FRAMES = 600 };
static const uint64_t MS = 1000000ull;

/* Protocol 2: up to four players around the host (see netplay.h). 4: the game state travels
 * compressed, in 1400-byte chunks. 5: games started together from power-on (WELCOME byte 7),
 * and the host's own analog sticks rounded like everyone else's. */
enum { PROTOCOL = 5 };
enum { T_ANNOUNCE = 1, T_HELLO, T_WELCOME, T_CHUNK, T_MISSING, T_READY, T_INPUT, T_BYE, T_QUERY, T_INFO, T_FRAMES };
enum { REFUSE_NONE, REFUSE_ROM, REFUSE_NO_STATE, REFUSE_BUSY, REFUSE_WAIT, REFUSE_STARTED };

/* Start together (netplay_set_boot_start): for cores whose save states do not carry the whole game
 * (MAME: measured on Area 51 and CarnEvil, a copied game drifts away from the host's within
 * seconds), no state travels. The host waits at power-on, without running a frame, until a guest
 * that has just loaded the same game is ready; both then run from frame 0 with the same inputs.
 * Once started, nobody else can join. */
static bool boot_start, boot_started;

static int sock = -1;
/* Guest: the host. Host: the guest receiving the game state. */
static struct sockaddr_in peer;
static bool have_peer, hosting;
static atomic_int state = NET_OFF;
static char rom_key[64], rom_title[64], fail_reason[256];
static uint32_t rom_crc;
static uint64_t started_ns, last_recv_ns, last_send_ns, last_announce_ns, last_hello_ns, last_ready_ns, last_frames_ns;
/* Guest: frames of input delay, picked from the measured round trip and told to the host. */
static int delay = DELAY_DEFAULT;

/* Internet hosting: the public address comes from the router (UPnP) or public STUN servers. */
static bool internet, upnp_mapped;
/* True when the router gave a public address (not behind a second NAT): STUN must not override it. */
static bool upnp_public;
static pthread_mutex_t public_lock = PTHREAD_MUTEX_INITIALIZER;
static struct sockaddr_in stun_list[4];
static int stun_count, stun_tries;
static atomic_bool helper_done;
static uint32_t public_ip;
static uint16_t public_port;
static uint8_t stun_txid[12];
static uint64_t last_stun_ns;

/* Lockstep. Every headset runs frame f with the same four guns. Guests send their own gun
 * ahead of time to the host; the host gathers all of them, runs the frame and sends the whole
 * frame (all guns) to every guest, which run it when it arrives. A slot holds frame f when its
 * tag is f + 1. */
static uint32_t frame;
static TremuGun local_in[RING];
static uint32_t local_tag[RING];
static TremuGun all_in[RING][NET_MAX_PLAYERS];
static uint32_t all_tag[RING];
static bool ready_unconfirmed;

/* Host: one entry per guest slot (players 2 to 4; slot 0 is the host). */
typedef struct Guest {
    bool used;     /* the slot belongs to this address (being sent the game, or playing) */
    bool playing;  /* in the lockstep */
    struct sockaddr_in addr;
    int delay;
    uint64_t last_recv_ns;
    TremuGun in[RING];
    uint32_t tag[RING];
    uint32_t crc_frame, crc;
    uint32_t checked_frame;  /* last frame whose CRC was compared: each mismatch counts once */
    int mismatches;          /* checks in a row that differed */
    uint32_t loaded_frame;   /* frame it loaded the game at: no checks for a while after */
} Guest;
static Guest guests[NET_MAX_PLAYERS];
static int sending_slot;  /* guest receiving the game state, 0 when none */
static atomic_int player_count = 1;
/* Host: its own RAM CRCs, to compare with what each guest reports. */
static uint32_t host_crc_frames[CRC_HISTORY], host_crcs[CRC_HISTORY];
/* Guest: which player this headset is, and whether the host is busy sending to a newcomer. */
static int my_slot;
static atomic_bool host_busy;

/* Game state transfer, host → one guest. The state goes compressed (zlib, fastest level: a
 * PS2 or Dreamcast state of tens of megabytes shrinks three to five times, and the few hundred
 * kilobytes of an arcade state cost nothing). The host paces its sends (a 700-packet burst
 * overflows Wi-Fi receive buffers); the guest asks again for what is missing. state_buf holds
 * the compressed bytes (packed_size of them) on both sides; state_size is the core's own size. */
enum { CHUNKS_PER_STEP = 8 };
static uint8_t *state_buf, *chunk_have;
static size_t state_size, packed_size;
static uint32_t chunk_count, chunks_received, send_cursor, missing_requests, transfer_frame;
static uint64_t last_progress_log_ns;
/* Guest: frames the game ran on its own while receiving. A state does not hold everything: a
 * Dreamcast's BIOS (Flycast's own) fills part of the RAM in the first frames, and a guest that
 * loaded the host's state without ever running kept that part different (the pictures agreed,
 * the RAM check did not). Two seconds of running first, then the state. */
enum { WARM_FRAMES = 120 };
static int warm_frames;

/* A transfer may take 30 s plus a second per megabyte on the wire: a big state on a slow Wi-Fi
 * is slow, not dead. */
static uint64_t transfer_timeout_ns(void) { return 30000 * MS + (uint64_t)packed_size * 1000; }

/* Sync checks: CRC of the game RAM every CRC_EVERY frames. */
static uint32_t local_crc_frame, local_crc;
static atomic_int desyncs;
/* Desync repair: the host sends its state again to that guest. resyncing marks such a transfer;
 * resync_times limits repairs when the game keeps diverging. */
static bool resyncing;
static uint64_t resync_times[3], last_welcome_ns;
static atomic_int resyncs;

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static void net_log(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "Tremu-net", "%s", line);
    if (log_file) {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        fprintf(log_file, "%02d-%02d %02d:%02d:%02d %s\n", tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, line);
        fflush(log_file);
    }
}

void netplay_set_log_file(const char *path) {
    if (log_file) fclose(log_file);
    log_file = NULL;
    if (!path) return;
    /* Kept below a megabyte: the last few games. */
    FILE *f = fopen(path, "a");
    if (f && ftell(f) > (1 << 20)) { fclose(f); f = fopen(path, "w"); }
    log_file = f;
}

void netplay_set_boot_start(bool on) { boot_start = on; }

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void header(uint8_t *p, int type) { p[0] = 'W'; p[1] = 'F'; p[2] = (uint8_t)type; p[3] = PROTOCOL; }
static bool same_addr(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static void send_to(const struct sockaddr_in *to, const uint8_t *buf, size_t len) {
    if (sock >= 0) sendto(sock, buf, len, 0, (const struct sockaddr *)to, sizeof *to);
    last_send_ns = now_ns();
}
static void send_peer(const uint8_t *buf, size_t len) {
    if (have_peer) send_to(&peer, buf, len);
}

/* 11 bytes per player: gun x, y, flag bits, then the Touch gamepad buttons and sticks. */
enum { GUN_BYTES = 11 };
static void pack_gun(uint8_t *p, const TremuGun *g) {
    p[0] = (uint8_t)g->x; p[1] = (uint8_t)((uint16_t)g->x >> 8);
    p[2] = (uint8_t)g->y; p[3] = (uint8_t)((uint16_t)g->y >> 8);
    p[4] = g->offscreen | g->trigger << 1 | g->reload << 2 | g->aux_a << 3 | g->aux_b << 4 | g->start << 5 | g->select << 6;
    p[5] = (uint8_t)g->pad; p[6] = (uint8_t)(g->pad >> 8);
    p[7] = (uint8_t)(g->lx >> 8); p[8] = (uint8_t)(g->ly >> 8); p[9] = (uint8_t)(g->rx >> 8); p[10] = (uint8_t)(g->ry >> 8);
}
static void unpack_gun(const uint8_t *p, TremuGun *g) {
    g->x = (int16_t)(p[0] | p[1] << 8);
    g->y = (int16_t)(p[2] | p[3] << 8);
    g->offscreen = p[4] & 1; g->trigger = p[4] >> 1 & 1; g->reload = p[4] >> 2 & 1; g->aux_a = p[4] >> 3 & 1;
    g->aux_b = p[4] >> 4 & 1; g->start = p[4] >> 5 & 1; g->select = p[4] >> 6 & 1;
    g->pad = (uint16_t)(p[5] | p[6] << 8);
    g->lx = (int16_t)((int8_t)p[7] * 256); g->ly = (int16_t)((int8_t)p[8] * 256);
    g->rx = (int16_t)((int8_t)p[9] * 256); g->ry = (int16_t)((int8_t)p[10] * 256);
}
static const TremuGun NEUTRAL = {.offscreen = true};

/* Blocking part of Internet hosting, off the emulator thread: UPnP mapping and STUN DNS. */
enum { ADHOC_SERVER_PORT = 27312, ADHOC_RELAY_PORT = 27313 };
static atomic_bool link_mode, link_ports_open;
static bool want_link;

static void *internet_helper(void *arg) {
    (void)arg;
    uint32_t external = 0;
    bool mapped = upnp_map_udp(NET_GAME_PORT, &external);
    /* PPSSPP's ad hoc server and relay: guests only ever connect to these two. */
    if (mapped && atomic_load(&link_mode))
        atomic_store(&link_ports_open, upnp_map_tcp(ADHOC_SERVER_PORT) && upnp_map_tcp(ADHOC_RELAY_PORT));
    struct sockaddr_in list[4];
    int n = stun_servers(list, 4);
    pthread_mutex_lock(&public_lock);
    upnp_mapped = mapped;
    /* A private external address means another NAT upstream (carrier): only STUN knows the truth. */
    bool private_ip = (external >> 24) == 10 || (external >> 22) == (100u << 2 | 1) ||
                      (external >> 20) == (172u << 4 | 1) || (external >> 16) == (192u << 8 | 168);
    if (mapped && external && !private_ip) {
        upnp_public = true;
        public_ip = external;
        public_port = NET_GAME_PORT;
    }
    memcpy(stun_list, list, sizeof list);
    stun_count = n;
    pthread_mutex_unlock(&public_lock);
    atomic_store(&helper_done, true);
    return NULL;
}

static void *unmap_helper(void *arg) {
    upnp_unmap_udp(NET_GAME_PORT);
    if (arg) {
        upnp_unmap_tcp(ADHOC_SERVER_PORT);
        upnp_unmap_tcp(ADHOC_RELAY_PORT);
    }
    return NULL;
}

/* Asks the STUN servers for our public address, then keeps the router mapping alive. */
static void internet_step(uint64_t now) {
    if (!internet || sock < 0) return;
    pthread_mutex_lock(&public_lock);
    bool known = public_ip != 0;
    int count = stun_count;
    struct sockaddr_in list[4];
    memcpy(list, stun_list, sizeof list);
    pthread_mutex_unlock(&public_lock);
    uint64_t interval = known ? 15000 * MS : 500 * MS;
    if (!count || now - last_stun_ns < interval || (!known && stun_tries >= 8)) return;
    uint8_t request[20];
    stun_request(request, stun_txid);
    for (int i = 0; i < count; i++) sendto(sock, request, sizeof request, 0, (struct sockaddr *)&list[i], sizeof list[i]);
    stun_tries++;
    last_stun_ns = now;
}

static bool open_socket(void) {
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    int buffer = 4 << 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof buffer);
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof buffer);
    /* TREMU_NET_PORT: another port, for tests running several players on one device. */
    const char *test_port = getenv("TREMU_NET_PORT");
    uint16_t port = test_port ? (uint16_t)atoi(test_port) : NET_GAME_PORT;
    struct sockaddr_in any = {.sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(sock, (struct sockaddr *)&any, sizeof any) < 0) {
        LOG("bind %d failed", port);
        close(sock);
        sock = -1;
        return false;
    }
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
    return true;
}

static void free_transfer(void) {
    free(state_buf);
    free(chunk_have);
    state_buf = chunk_have = NULL;
    state_size = packed_size = 0;
    chunk_count = chunks_received = send_cursor = missing_requests = 0;
}

/* The lockstep (re)starts at frame start: frames start .. start+d-1 of a gun are neutral. */
static void fill_neutral(TremuGun *ring, uint32_t *tag, uint32_t start, int d) {
    for (uint32_t f = start; f < start + (uint32_t)d; f++)
        if (tag[f % RING] != f + 1) {
            ring[f % RING] = NEUTRAL;
            tag[f % RING] = f + 1;
        }
}

static void fail(const char *reason) {
    snprintf(fail_reason, sizeof fail_reason, "%s", reason);
    LOG("failed: %s", reason);
    atomic_store(&state, NET_FAILED);
}

static int playing_guests(void) {
    int n = 0;
    for (int s = 1; s < NET_MAX_PLAYERS; s++) n += guests[s].playing;
    return n;
}

static void update_player_count(void) {
    atomic_store(&player_count, 1 + playing_guests());
}

bool netplay_host(const char *key, const char *title, uint32_t crc, bool over_internet) {
    netplay_stop();
    atomic_store(&link_mode, want_link);
    atomic_store(&link_ports_open, false);
    memset(resync_times, 0, sizeof resync_times);
    resyncing = false;
    atomic_store(&resyncs, 0);
    if (!open_socket()) return false;
    internet = over_internet;
    if (internet) {
        pthread_mutex_lock(&public_lock);
        public_ip = 0;
        public_port = 0;
        upnp_mapped = upnp_public = false;
        stun_count = 0;
        pthread_mutex_unlock(&public_lock);
        stun_tries = 0;
        last_stun_ns = 0;
        atomic_store(&helper_done, false);
        pthread_t helper;
        if (!pthread_create(&helper, NULL, internet_helper, NULL)) pthread_detach(helper);
    }
    snprintf(rom_key, sizeof rom_key, "%s", key);
    snprintf(rom_title, sizeof rom_title, "%s", title);
    rom_crc = crc;
    hosting = true;
    have_peer = false;
    memset(guests, 0, sizeof guests);
    sending_slot = 0;
    frame = 0;
    memset(local_tag, 0, sizeof local_tag);
    memset(all_tag, 0, sizeof all_tag);
    memset(host_crc_frames, 0, sizeof host_crc_frames);
    atomic_store(&desyncs, 0);
    boot_started = false;
    update_player_count();
    atomic_store(&state, NET_HOSTING);
    LOG("hosting %s (crc %08x)%s%s", key, crc, internet ? " on the Internet" : "",
        boot_start ? ", starting together from power-on" : "");
    return true;
}

bool netplay_host_link(const char *key, const char *title, bool over_internet) {
    want_link = true;
    bool ok = netplay_host(key, title, 0, over_internet);
    want_link = false;
    if (ok) atomic_store(&state, NET_LINK);
    return ok;
}

bool netplay_local_ip(char *out, size_t size) {
    struct ifaddrs *list, *it;
    bool found = false;
    if (getifaddrs(&list)) return false;
    for (it = list; it && !found; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || (it->ifa_flags & IFF_LOOPBACK) || !(it->ifa_flags & IFF_UP)) continue;
        found = inet_ntop(AF_INET, &((struct sockaddr_in *)it->ifa_addr)->sin_addr, out, (socklen_t)size) != NULL;
    }
    freeifaddrs(list);
    return found;
}

bool netplay_join(const char *host, const char *key, uint32_t crc) {
    netplay_stop();
    atomic_store(&resyncs, 0);
    if (!open_socket()) { fail(T("R\xC3\xA9seau indisponible")); return false; }
    snprintf(rom_key, sizeof rom_key, "%s", key);
    rom_crc = crc;
    hosting = false;
    /* host is "ip" (local network, game port) or "ip:port" (Internet code). */
    char ip[64];
    snprintf(ip, sizeof ip, "%s", host);
    char *colon = strchr(ip, ':');
    uint16_t port = NET_GAME_PORT;
    if (colon) { *colon = 0; port = (uint16_t)atoi(colon + 1); }
    peer = (struct sockaddr_in){.sin_family = AF_INET, .sin_port = htons(port)};
    inet_pton(AF_INET, ip, &peer.sin_addr);
    have_peer = true;
    started_ns = now_ns();
    last_hello_ns = 0;
    my_slot = 0;
    atomic_store(&host_busy, false);
    atomic_store(&state, NET_JOINING);
    LOG("joining %s for %s (crc %08x)", host, key, crc);
    return true;
}

void netplay_stop(void) {
    uint8_t bye[4];
    header(bye, T_BYE);
    if (sock >= 0 && hosting)
        for (int s = 1; s < NET_MAX_PLAYERS; s++)
            if (guests[s].used) send_to(&guests[s].addr, bye, sizeof bye);
    if (sock >= 0 && !hosting && have_peer) send_peer(bye, sizeof bye);
    if (sock >= 0) close(sock);
    sock = -1;
    have_peer = hosting = false;
    memset(guests, 0, sizeof guests);
    sending_slot = 0;
    update_player_count();
    if (upnp_mapped) {
        pthread_t helper;
        if (!pthread_create(&helper, NULL, unmap_helper, atomic_load(&link_ports_open) ? &link_ports_open : NULL)) pthread_detach(helper);
        upnp_mapped = false;
    }
    atomic_store(&link_mode, false);
    upnp_public = false;
    internet = false;
    free_transfer();
    if (atomic_load(&state) != NET_FAILED) atomic_store(&state, NET_OFF);
}

NetState netplay_state(void) { return (NetState)atomic_load(&state); }
uint32_t netplay_frame_index(void) { return frame; }
bool netplay_is_host(void) { return hosting; }
bool netplay_is_internet(void) { return internet; }
int netplay_players(void) { return atomic_load(&player_count); }

bool netplay_transferring(void) {
    int st = atomic_load(&state);
    return st == NET_JOINING || st == NET_SENDING || st == NET_RECEIVING;
}

void netplay_dismiss_failure(void) {
    int failed = NET_FAILED;
    atomic_compare_exchange_strong(&state, &failed, NET_OFF);
}

/* WELCOME: refusal, the guest's slot, resync flag, state size, chunks, the frame it starts at
 * and the compressed size. */
/* When the HELLO being answered arrived: the WELCOME says how long the host held it (packing a
 * Dreamcast state takes 150 ms), so the guest's round trip, hence its input delay, is the
 * network's alone. */
static uint64_t hello_rx_ns;

static void send_welcome_to(const struct sockaddr_in *to, int refuse, int slot) {
    uint8_t p[27] = {0};
    /* The cores draw their own gun sights into the picture the headsets compare: everyone shows
     * them as the host does. */
    p[24] = tremu_core_crosshair();
    uint64_t held = (now_ns() - hello_rx_ns) / MS;
    if (held > 65535) held = 65535;
    p[25] = (uint8_t)held;
    p[26] = (uint8_t)(held >> 8);
    header(p, T_WELCOME);
    p[4] = (uint8_t)refuse;
    p[5] = (uint8_t)slot;
    p[6] = resyncing;
    p[7] = boot_start;  /* no state follows: start from power-on, at frame transfer_frame (0) */
    put32(p + 8, (uint32_t)state_size);
    put32(p + 12, chunk_count);
    put32(p + 16, transfer_frame);
    put32(p + 20, (uint32_t)packed_size);
    send_to(to, p, sizeof p);
}

static void send_chunk(uint32_t i) {
    uint8_t p[12 + CHUNK];
    size_t off = (size_t)i * CHUNK, len = packed_size - off < CHUNK ? packed_size - off : CHUNK;
    header(p, T_CHUNK);
    put32(p + 4, i);
    put32(p + 8, (uint32_t)len);
    memcpy(p + 12, state_buf + off, len);
    send_peer(p, 12 + len);
}

/* Host: snapshots the game at the current frame and starts sending it to one guest. The whole
 * party waits meanwhile (the host runs no frame until that guest is ready). */
static bool start_sending(int slot) {
    free_transfer();
    uint64_t t0 = now_ns();
    state_size = tremu_core_state_size();
    uint8_t *raw = state_size ? malloc(state_size) : NULL;
    if (!raw || !tremu_core_save_state(raw, state_size)) {
        free(raw);
        free_transfer();
        return false;
    }
    uLongf packed = compressBound((uLong)state_size);
    state_buf = malloc(packed);
    if (!state_buf || compress2(state_buf, &packed, raw, (uLong)state_size, Z_BEST_SPEED) != Z_OK) {
        free(raw);
        free_transfer();
        return false;
    }
    free(raw);
    packed_size = packed;
    LOG("state %zu bytes packed to %zu in %llu ms", state_size, packed_size, (unsigned long long)((now_ns() - t0) / MS));
    chunk_count = (uint32_t)((packed_size + CHUNK - 1) / CHUNK);
    transfer_frame = frame;
    sending_slot = slot;
    peer = guests[slot].addr;
    have_peer = true;
    guests[slot].playing = false;
    send_welcome_to(&peer, REFUSE_NONE, slot);
    last_welcome_ns = now_ns();
    send_cursor = 0;  /* chunks go out a few per step, see netplay_frame */
    started_ns = now_ns();
    update_player_count();
    atomic_store(&state, NET_SENDING);
    return true;
}

static int slot_of(const struct sockaddr_in *from) {
    for (int s = 1; s < NET_MAX_PLAYERS; s++)
        if (guests[s].used && same_addr(&guests[s].addr, from)) return s;
    return 0;
}

/* Host: a headset asked to join. */
static void accept_guest(const struct sockaddr_in *from, const uint8_t *p, ssize_t n) {
    hello_rx_ns = now_ns();
    int slot = slot_of(from);
    if (slot && sending_slot == slot) { send_welcome_to(from, REFUSE_NONE, slot); return; }  /* our welcome was lost */
    if (slot && guests[slot].playing) return;  /* late duplicate */
    uint32_t crc = get32(p + 4);
    const char *key = (const char *)p + 8;
    if (n < 9 || p[n - 1] != 0 || strcmp(key, rom_key) || crc != rom_crc) {
        LOG("guest refused: rom %s %08x vs %s %08x", n > 8 ? key : "?", crc, rom_key, rom_crc);
        send_welcome_to(from, REFUSE_ROM, 0);
        return;
    }
    if (sending_slot) { send_welcome_to(from, REFUSE_WAIT, 0); return; }  /* one newcomer at a time */
    if (boot_start && (boot_started || frame)) { send_welcome_to(from, REFUSE_STARTED, 0); return; }
    for (slot = 1; slot < NET_MAX_PLAYERS && guests[slot].used; slot++) {}
    if (slot == NET_MAX_PLAYERS) { send_welcome_to(from, REFUSE_BUSY, 0); return; }
    guests[slot] = (Guest){.used = true, .addr = *from, .last_recv_ns = now_ns()};
    resyncing = false;
    if (boot_start) {
        /* Start together: the guest has just loaded the game and run no frame, nor has the host. */
        free_transfer();
        memset(local_tag, 0, sizeof local_tag);
        memset(all_tag, 0, sizeof all_tag);
        fill_neutral(local_in, local_tag, 0, DELAY_DEFAULT);
        transfer_frame = 0;
        sending_slot = slot;
        peer = guests[slot].addr;
        have_peer = true;
        send_welcome_to(&peer, REFUSE_NONE, slot);
        last_welcome_ns = started_ns = now_ns();
        atomic_store(&state, NET_SENDING);
        LOG("player %d (%s) accepted: both start from power-on", slot + 1, inet_ntoa(from->sin_addr));
        return;
    }
    if (playing_guests() == 0) {
        /* First guest: the lockstep starts from here; the host's own inputs start neutral. */
        memset(local_tag, 0, sizeof local_tag);
        memset(all_tag, 0, sizeof all_tag);
        fill_neutral(local_in, local_tag, frame, DELAY_DEFAULT);
    }
    if (!start_sending(slot)) {
        send_welcome_to(from, REFUSE_NO_STATE, 0);
        guests[slot].used = false;
        return;
    }
    LOG("player %d (%s) accepted at frame %u, sending %zu bytes in %u chunks", slot + 1, inet_ntoa(from->sin_addr),
        frame, state_size, chunk_count);
}

static void drop_guest(int slot, const char *why) {
    LOG("player %d left (%s)", slot + 1, why);
    if (sending_slot == slot) {
        sending_slot = 0;
        have_peer = false;
        free_transfer();
    }
    guests[slot] = (Guest){0};
    update_player_count();
    atomic_store(&state, playing_guests() ? NET_PLAYING : NET_HOSTING);
}

/* Host: a guest's CRC against the host's own for the same frame. */
static void check_guest_crc(int slot) {
    Guest *g = &guests[slot];
    if (!g->crc_frame || g->crc_frame <= g->checked_frame) return;
    /* Right after loading a state, part of the RAM differs for a few seconds and then agrees again
     * (Flycast, House of the Dead 2): comparing then repaired a sound game, which loaded a state
     * again, and so on, freezing the game for 5 s each time. */
    if (g->crc_frame < g->loaded_frame + SETTLE_FRAMES) return;
    for (int i = 0; i < CRC_HISTORY; i++) {
        if (!host_crc_frames[i] || host_crc_frames[i] != g->crc_frame) continue;
        g->checked_frame = g->crc_frame;
        if (host_crcs[i] == g->crc) { g->mismatches = 0; return; }
        /* The check compares pictures: a message an emulator shows for a few seconds of real time
         * (MAME at power-on) differs for a moment while the games are the same. A diverged game
         * differs at every check: three in a row (3 s) before calling it a desync. */
        LOG("player %d picture differs at frame %u: host %08x guest %08x", slot + 1, g->crc_frame, host_crcs[i], g->crc);
        /* GPU cores compare RAM, which can differ for ten seconds or more while the picture stays the
         * same (Flycast, House of the Dead 2), and their state is big (a Dreamcast's takes 5 s and
         * more to send, the game frozen meanwhile): only a difference that lasts 30 s is a desync. */
        if (++g->mismatches != (tremu_core_sync_is_picture() ? 3 : 30)) return;
        atomic_fetch_add(&desyncs, 1);
        LOG("player %d desync at frame %u", slot + 1, g->crc_frame);
        /* Started together: a copied state would not carry the whole game either. */
        if (sending_slot || boot_start) return;
        /* Repair from the host's game, at most three times a minute (a game that keeps
         * diverging would otherwise spend its time transferring). */
        uint64_t now = now_ns();
        if (resync_times[0] && now - resync_times[0] < 60000 * MS) {
            LOG("desync repeats too often, not resyncing");
            return;
        }
        memmove(resync_times, resync_times + 1, sizeof resync_times - sizeof resync_times[0]);
        resync_times[2] = now;
        resyncing = true;
        if (start_sending(slot)) {
            atomic_fetch_add(&resyncs, 1);
            LOG("resync: sending %zu bytes again to player %d", state_size, slot + 1);
        } else {
            resyncing = false;
        }
        return;
    }
}

/* Guest: the host announces a state transfer to us (first join or desync repair). */
static bool start_receiving(const uint8_t *p) {
    free_transfer();
    my_slot = p[5];
    state_size = get32(p + 8);
    chunk_count = get32(p + 12);
    transfer_frame = get32(p + 16);
    packed_size = get32(p + 20);
    state_buf = malloc(packed_size);
    chunk_have = calloc(chunk_count, 1);
    if (!state_buf || !chunk_have || !state_size || my_slot < 1 || my_slot >= NET_MAX_PLAYERS ||
        chunk_count != (packed_size + CHUNK - 1) / CHUNK) {
        fail(T("M\xC3\xA9moire insuffisante"));
        return false;
    }
    started_ns = now_ns();
    warm_frames = 0;
    atomic_store(&state, NET_RECEIVING);
    return true;
}

static void receive(void) {
    uint8_t p[1600];
    struct sockaddr_in from;
    socklen_t from_len;
    ssize_t n;
    while (sock >= 0 && (from_len = sizeof from, n = recvfrom(sock, p, sizeof p, 0, (struct sockaddr *)&from, &from_len)) >= 4) {
        if (n >= 20 && p[0] == 0x01 && p[1] == 0x01) {  /* STUN answer */
            uint32_t ip;
            uint16_t port;
            if (internet && stun_parse(p, (size_t)n, stun_txid, &ip, &port)) {
                pthread_mutex_lock(&public_lock);
                bool changed = !upnp_public && (ip != public_ip || port != public_port);
                if (!upnp_public) { public_ip = ip; public_port = port; }
                pthread_mutex_unlock(&public_lock);
                if (changed) LOG("STUN: public address %u.%u.%u.%u:%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255, port);
            }
            continue;
        }
        if (p[0] != 'W' || p[1] != 'F' || p[3] != PROTOCOL) continue;
        int st = atomic_load(&state);
        int slot = hosting ? slot_of(&from) : 0;
        bool from_host = !hosting && have_peer && same_addr(&from, &peer);
        if (slot) guests[slot].last_recv_ns = now_ns();
        if (from_host) last_recv_ns = now_ns();
        switch (p[2]) {
        case T_HELLO:
            if (hosting && st != NET_LINK && n >= 9) accept_guest(&from, p, n);
            break;
        case T_QUERY:
            /* Someone with our party code asks which game this is. */
            if (hosting) {
                uint8_t r[4 + 64 + 64];
                header(r, T_INFO);
                size_t a = strlen(rom_key) + 1, b = strlen(rom_title) + 1;
                memcpy(r + 4, rom_key, a);
                memcpy(r + 4 + a, rom_title, b);
                sendto(sock, r, 4 + a + b, 0, (struct sockaddr *)&from, sizeof from);
            }
            break;
        case T_WELCOME:
            if (!from_host || n < 24) break;
            if (p[4] == REFUSE_NONE && p[6] && (st == NET_PLAYING || st == NET_STALLED)) {
                if (start_receiving(p)) {
                    atomic_fetch_add(&resyncs, 1);
                    LOG("resync: receiving %zu bytes from the host", state_size);
                }
                break;
            }
            if (st != NET_JOINING) break;
            if (p[4] == REFUSE_WAIT) { started_ns = now_ns(); break; }  /* another player is joining: keep asking */
            if (p[4] != REFUSE_NONE) {
                fail(T(p[4] == REFUSE_ROM ? "ROM diff\xC3\xA9rente de celle de l'h\xC3\xB4te"
                     : p[4] == REFUSE_BUSY ? "La partie a d\xC3\xA9j\xC3\xA0 quatre joueurs"
                     : p[4] == REFUSE_STARTED ? "Partie d\xC3\xA9j\xC3\xA0 commenc\xC3\xA9" "e : l'h\xC3\xB4te doit choisir H\xC3\xA9" "berger \xC3\xA0 nouveau"
                                              : "L'h\xC3\xB4te ne peut pas partager sa partie"));
                break;
            }
            {
                /* Input delay from the round trip: half of it, in 60 Hz frames, plus one. */
                uint64_t rtt = now_ns() - last_hello_ns;
                uint64_t held = n >= 27 ? (uint64_t)(p[25] | p[26] << 8) * MS : 0;
                rtt = rtt > held ? rtt - held : 0;
                int frames = (int)((rtt / 2 + 16666666ull) / 16666667ull) + 1;
                delay = frames < DELAY_DEFAULT ? DELAY_DEFAULT : frames > DELAY_MAX ? DELAY_MAX : frames;
                my_slot = p[5];
                LOG("player %d, round trip %llu ms, input delay %d frames", my_slot + 1, (unsigned long long)(rtt / MS), delay);
            }
            if (n >= 25) {
                LOG("gun sights %s, as on the host", p[24] ? "shown" : "hidden");
                tremu_core_set_crosshair(p[24] != 0);
            }
            if (p[7]) {
                /* Start together: this headset has just loaded the game and run no frame. */
                if (my_slot < 1 || my_slot >= NET_MAX_PLAYERS) { fail(T("L'h\xC3\xB4te ne peut pas partager sa partie")); break; }
                free_transfer();
                frame = get32(p + 16);
                memset(local_tag, 0, sizeof local_tag);
                memset(all_tag, 0, sizeof all_tag);
                fill_neutral(local_in, local_tag, frame, delay);
                local_crc_frame = 0;
                atomic_store(&desyncs, 0);
                ready_unconfirmed = true;
                last_ready_ns = 0;
                last_recv_ns = now_ns();
                atomic_store(&state, NET_PLAYING);
                LOG("welcome: player %d, both start from power-on at frame %u", my_slot + 1, frame);
                break;
            }
            if (!start_receiving(p)) break;
            LOG("welcome: %zu bytes (%zu packed) in %u chunks, from frame %u", state_size, packed_size, chunk_count, transfer_frame);
            break;
        case T_CHUNK: {
            if (st != NET_RECEIVING || !from_host || n < 12) break;
            uint32_t i = get32(p + 4), len = get32(p + 8);
            size_t off = (size_t)i * CHUNK;
            if (i >= chunk_count || chunk_have[i] || off + len > packed_size || 12 + len > (uint32_t)n) break;
            memcpy(state_buf + off, p + 12, len);
            chunk_have[i] = 1;
            chunks_received++;
            break;
        }
        case T_MISSING: {
            if (st != NET_SENDING || !slot || slot != sending_slot || n < 16) break;
            resyncing = false;  /* the guest is receiving: no more welcomes needed */
            /* Resend from the first chunk the guest lacks: rewinding the cursor paces it like the first pass. */
            uint32_t first = get32(p + 4);
            if (first < send_cursor) send_cursor = first;
            missing_requests++;
            break;
        }
        case T_READY:
            if (st == NET_SENDING && slot && slot == sending_slot) {
                Guest *g = &guests[slot];
                g->delay = n >= 5 ? (p[4] < DELAY_DEFAULT ? DELAY_DEFAULT : p[4] > DELAY_MAX ? DELAY_MAX : p[4]) : DELAY_DEFAULT;
                memset(g->tag, 0, sizeof g->tag);
                fill_neutral(g->in, g->tag, transfer_frame, g->delay);
                g->playing = true;
                g->crc_frame = g->checked_frame = 0;
                g->mismatches = 0;
                g->loaded_frame = transfer_frame;
                sending_slot = 0;
                have_peer = false;
                free_transfer();
                resyncing = false;
                if (boot_start) boot_started = true;
                update_player_count();
                atomic_store(&state, NET_PLAYING);
                LOG("player %d ready at frame %u with %d frames of delay (%d players)", slot + 1, frame, g->delay,
                    atomic_load(&player_count));
            }
            break;
        case T_INPUT: {
            /* Guest → host: its own gun for a run of frames, plus its last RAM CRC. */
            if (!slot || !guests[slot].playing || n < 17) break;
            Guest *g = &guests[slot];
            uint32_t first = get32(p + 4);
            int count = p[8];
            if (17 + count * GUN_BYTES > n) break;
            for (int k = 0; k < count; k++) {
                uint32_t f = first + k;
                if (f < frame || g->tag[f % RING] == f + 1) continue;
                unpack_gun(p + 17 + k * GUN_BYTES, &g->in[f % RING]);
                g->tag[f % RING] = f + 1;
            }
            if (get32(p + 9) > g->crc_frame) {
                g->crc_frame = get32(p + 9);
                g->crc = get32(p + 13);
                check_guest_crc(slot);
            }
            break;
        }
        case T_FRAMES: {
            /* Host → guest: whole frames (all guns). [4] count, [5] host busy, [8] first frame. */
            if (!from_host || n < 12 || (st != NET_PLAYING && st != NET_STALLED)) break;
            int count = p[4];
            atomic_store(&host_busy, p[5] != 0);
            atomic_store(&player_count, p[6] ? p[6] : 1);
            uint32_t first = get32(p + 8);
            if (12 + count * GUN_BYTES * NET_MAX_PLAYERS > n) break;
            for (int k = 0; k < count; k++) {
                uint32_t f = first + k;
                if (f < frame || all_tag[f % RING] == f + 1) continue;
                for (int s = 0; s < NET_MAX_PLAYERS; s++) unpack_gun(p + 12 + (k * NET_MAX_PLAYERS + s) * GUN_BYTES, &all_in[f % RING][s]);
                all_tag[f % RING] = f + 1;
            }
            ready_unconfirmed = false;
            break;
        }
        case T_BYE:
            if (slot) drop_guest(slot, "bye");
            else if (from_host) fail(T("L'h\xC3\xB4te a quitt\xC3\xA9 la partie"));
            break;
        }
    }
}

static void announce(uint64_t now) {
    if (now - last_announce_ns < 1000 * MS || getenv("TREMU_NO_ANNOUNCE")) return;  /* test bench: unseen */
    last_announce_ns = now;
    uint8_t p[4 + 64 + 64];
    header(p, T_ANNOUNCE);
    size_t a = strlen(rom_key) + 1, b = strlen(rom_title) + 1;
    memcpy(p + 4, rom_key, a);
    memcpy(p + 4 + a, rom_title, b);
    struct sockaddr_in all = {.sin_family = AF_INET, .sin_port = htons(NET_DISCOVERY_PORT),
                              .sin_addr.s_addr = htonl(INADDR_BROADCAST)};
    sendto(sock, p, 4 + a + b, 0, (struct sockaddr *)&all, sizeof all);
}

/* Guest → host: its own gun for the last frames (repeated against losses) and its CRC. */
static void send_inputs(void) {
    uint32_t last = frame + delay, first = last + 1 > REDUNDANT ? last + 1 - REDUNDANT : 0;
    uint8_t p[17 + REDUNDANT * GUN_BYTES];
    header(p, T_INPUT);
    int count = 0;
    for (uint32_t f = first; f <= last; f++) {
        if (local_tag[f % RING] != f + 1) continue;
        if (!count) put32(p + 4, f);
        pack_gun(p + 17 + count++ * GUN_BYTES, &local_in[f % RING]);
    }
    if (!count) return;
    p[8] = (uint8_t)count;
    put32(p + 9, local_crc_frame);
    put32(p + 13, local_crc);
    send_peer(p, 17 + count * GUN_BYTES);
}

/* Host → every guest: the last whole frames (repeated against losses). */
static void send_frames(void) {
    uint8_t p[12 + REDUNDANT * GUN_BYTES * NET_MAX_PLAYERS];
    header(p, T_FRAMES);
    uint32_t first = frame > REDUNDANT ? frame - REDUNDANT : 0;
    int count = 0;
    for (uint32_t f = first; f < frame; f++) {
        if (all_tag[f % RING] != f + 1) continue;
        if (!count) put32(p + 8, f);
        for (int s = 0; s < NET_MAX_PLAYERS; s++) pack_gun(p + 12 + (count * NET_MAX_PLAYERS + s) * GUN_BYTES, &all_in[f % RING][s]);
        count++;
    }
    if (!count) put32(p + 8, frame);
    p[4] = (uint8_t)count;
    p[5] = sending_slot != 0;
    p[6] = (uint8_t)atomic_load(&player_count);
    p[7] = 0;
    for (int s = 1; s < NET_MAX_PLAYERS; s++)
        if (guests[s].playing) send_to(&guests[s].addr, p, 12 + count * GUN_BYTES * NET_MAX_PLAYERS);
    last_frames_ns = now_ns();
}

static void request_missing(void) {
    uint32_t first = 0;
    while (first < chunk_count && chunk_have[first]) first++;
    uint8_t p[16] = {0};
    header(p, T_MISSING);
    put32(p + 4, first);
    for (uint32_t bit = 0; bit < 64 && first + bit < chunk_count; bit++)
        if (!chunk_have[first + bit]) p[8 + bit / 8] |= 1 << (bit % 8);
    send_peer(p, sizeof p);
}

/* Host: guests silent for 10 s leave the party, so the others can go on. */
static void drop_silent_guests(uint64_t now) {
    for (int s = 1; s < NET_MAX_PLAYERS; s++)
        if (guests[s].used && now - guests[s].last_recv_ns > 10000 * MS) drop_guest(s, "silent");
}

static bool host_frame(const TremuGun *local, TremuGun guns[NET_MAX_PLAYERS], uint64_t now) {
    uint32_t ahead = frame + DELAY_DEFAULT;
    bool fresh = local_tag[ahead % RING] != ahead + 1;
    if (fresh) {
        /* Through the wire format, as the guests will see it (the sticks travel in 8 bits). */
        uint8_t wire[GUN_BYTES];
        pack_gun(wire, local);
        unpack_gun(wire, &local_in[ahead % RING]);
        local_tag[ahead % RING] = ahead + 1;
    }
    if (now - last_frames_ns > 10 * MS) send_frames();
    bool waiting = false, silent = false;
    for (int s = 1; s < NET_MAX_PLAYERS; s++) {
        if (!guests[s].playing || guests[s].tag[frame % RING] == frame + 1) continue;
        waiting = true;
        silent = silent || now - guests[s].last_recv_ns > 3000 * MS;
    }
    drop_silent_guests(now);
    if (waiting) {
        if (silent) atomic_store(&state, NET_STALLED);
        return false;
    }
    TremuGun *row = all_in[frame % RING];
    row[0] = local_tag[frame % RING] == frame + 1 ? local_in[frame % RING] : NEUTRAL;
    for (int s = 1; s < NET_MAX_PLAYERS; s++) row[s] = guests[s].playing ? guests[s].in[frame % RING] : NEUTRAL;
    all_tag[frame % RING] = frame + 1;
    memcpy(guns, row, sizeof row[0] * NET_MAX_PLAYERS);
    frame++;
    send_frames();
    atomic_store(&state, NET_PLAYING);
    return true;
}

static bool guest_frame(const TremuGun *local, TremuGun guns[NET_MAX_PLAYERS], uint64_t now) {
    if (ready_unconfirmed && now - last_ready_ns > 100 * MS) {
        uint8_t p[5];
        header(p, T_READY);
        p[4] = (uint8_t)delay;
        send_peer(p, sizeof p);
        last_ready_ns = now;
    }
    uint32_t ahead = frame + delay;
    bool fresh = local_tag[ahead % RING] != ahead + 1;
    if (fresh) {
        local_in[ahead % RING] = *local;
        local_tag[ahead % RING] = ahead + 1;
    }
    if (fresh || now - last_send_ns > 10 * MS) send_inputs();
    if (all_tag[frame % RING] != frame + 1) {
        if (now - last_recv_ns > 3000 * MS) atomic_store(&state, NET_STALLED);
        return false;
    }
    memcpy(guns, all_in[frame % RING], sizeof all_in[0][0] * NET_MAX_PLAYERS);
    frame++;
    atomic_store(&state, NET_PLAYING);
    return true;
}

bool netplay_frame(const TremuGun *local, TremuGun guns[NET_MAX_PLAYERS]) {
    receive();
    /* Read the clock after receive(): handlers stamp started_ns, and an earlier "now" would make
     * now - started_ns wrap around and look like a timeout. */
    uint64_t now = now_ns();
    int st = atomic_load(&state);
    if (hosting && st != NET_OFF && atomic_load(&player_count) < NET_MAX_PLAYERS) announce(now);
    if (hosting) internet_step(now);
    switch (st) {
    case NET_HOSTING:
        /* Start together: nothing runs until the first guest is ready (see boot_start). */
        if (boot_start && !boot_started) return false;
        /* Alone: the game runs freely; frame counts on so a newcomer starts from here. */
        guns[0] = *local;
        for (int s = 1; s < NET_MAX_PLAYERS; s++) guns[s] = NEUTRAL;
        frame++;
        return true;
    case NET_SENDING:
        if ((resyncing || boot_start) && now - last_welcome_ns > 500 * MS) {
            send_welcome_to(&peer, REFUSE_NONE, sending_slot);
            last_welcome_ns = now;
        }
        if (now - last_frames_ns > 50 * MS) send_frames();  /* keeps the others waiting, not lost */
        for (int k = 0; k < CHUNKS_PER_STEP && send_cursor < chunk_count; k++) send_chunk(send_cursor++);
        if (now - last_progress_log_ns > 1000 * MS) {
            LOG("sending: cursor %u/%u, %u resend requests", send_cursor, chunk_count, missing_requests);
            last_progress_log_ns = now;
        }
        if (now - started_ns > transfer_timeout_ns()) {
            LOG("transfer timed out");
            drop_guest(sending_slot, "transfer timed out");
        }
        return false;
    case NET_JOINING:
        if (now - started_ns > 8000 * MS) { fail(T("H\xC3\xB4te introuvable sur le Wi-Fi")); return false; }
        if (now - last_hello_ns > 250 * MS) {
            uint8_t p[8 + 64];
            header(p, T_HELLO);
            put32(p + 4, rom_crc);
            size_t len = strlen(rom_key) + 1;
            memcpy(p + 8, rom_key, len);
            send_peer(p, 8 + len);
            last_hello_ns = now;
        }
        return false;
    case NET_RECEIVING:
        if (now - last_progress_log_ns > 1000 * MS) {
            LOG("receiving: %u/%u chunks", chunks_received, chunk_count);
            last_progress_log_ns = now;
        }
        if (now - started_ns > transfer_timeout_ns()) {
            LOG("transfer stopped at %u/%u chunks", chunks_received, chunk_count);
            fail(T("Transfert de la partie interrompu"));
            return false;
        }
        if (chunks_received < chunk_count || warm_frames < WARM_FRAMES) {
            /* Ask for the gap once the host has had time to send past it. */
            if (chunks_received < chunk_count && now - last_send_ns > 300 * MS) request_missing();
            /* Meanwhile the game runs here alone (its picture stays on, and see warm_frames). */
            for (int s = 0; s < NET_MAX_PLAYERS; s++) guns[s] = NEUTRAL;
            return true;
        }
        {
            uint8_t *raw = malloc(state_size);
            uLongf unpacked = (uLongf)state_size;
            bool ok = raw && uncompress(raw, &unpacked, state_buf, (uLong)packed_size) == Z_OK && unpacked == state_size;
            if (!ok) LOG("state unpack failed (%zu packed, %zu expected)", packed_size, state_size);
            ok = ok && tremu_core_load_state(raw, state_size);
            free(raw);
            if (!ok) { fail(T("\xC3\x89tat de partie incompatible")); return false; }
        }
        free_transfer();
        frame = transfer_frame;
        memset(local_tag, 0, sizeof local_tag);
        memset(all_tag, 0, sizeof all_tag);
        fill_neutral(local_in, local_tag, frame, delay);
        local_crc_frame = 0;
        atomic_store(&desyncs, 0);
        ready_unconfirmed = true;
        last_ready_ns = 0;
        last_recv_ns = now;
        atomic_store(&state, NET_PLAYING);
        LOG("state loaded, player %d starts at frame %u", my_slot + 1, frame);
        /* The next call starts the lockstep; never run a frame here (netplay_idle calls this too). */
        return false;
    case NET_PLAYING:
    case NET_STALLED:
        return hosting ? host_frame(local, guns, now) : guest_frame(local, guns, now);
    default:
        return false;
    }
}

void netplay_idle(void) {
    NetState st = (NetState)atomic_load(&state);
    if (st == NET_PLAYING || st == NET_STALLED) {
        /* Game paused here: keep the links alive without advancing any frame. */
        receive();
        uint64_t now = now_ns();
        if (hosting) {
            if (now - last_frames_ns > 100 * MS) send_frames();
            drop_silent_guests(now);
        } else if (now - last_send_ns > 100 * MS) {
            send_inputs();
        }
        return;
    }
    if (st == NET_HOSTING || st == NET_LINK) {  /* paused alone: still announced, still reachable */
        receive();
        uint64_t now = now_ns();
        announce(now);
        internet_step(now);
        return;
    }
    /* Handshake and transfer states never advance the game. */
    TremuGun unused[NET_MAX_PLAYERS];
    netplay_frame(&NEUTRAL, unused);
}

void netplay_after_frame(void) {
    if (atomic_load(&state) == NET_RECEIVING) warm_frames++;
    if (atomic_load(&state) != NET_PLAYING || frame % CRC_EVERY) return;
    local_crc = tremu_core_sync_crc();
    local_crc_frame = frame;
    if (hosting) {
        memmove(host_crc_frames, host_crc_frames + 1, sizeof host_crc_frames - sizeof host_crc_frames[0]);
        memmove(host_crcs, host_crcs + 1, sizeof host_crcs - sizeof host_crcs[0]);
        host_crc_frames[CRC_HISTORY - 1] = frame;
        host_crcs[CRC_HISTORY - 1] = local_crc;
        for (int s = 1; s < NET_MAX_PLAYERS; s++)
            if (guests[s].playing && guests[s].crc_frame) check_guest_crc(s);
    }
}

void netplay_status(char *out, size_t size) {
    int d = atomic_load(&desyncs), players = atomic_load(&player_count);
    switch (atomic_load(&state)) {
    case NET_LINK: {
        char code[12];
        if (!internet) snprintf(out, size, T("PSP ad hoc : visible sur le Wi-Fi, ouvre le mode multijoueur du jeu"));
        else if (!netplay_code(code)) snprintf(out, size, T("Recherche de l'adresse publique..."));
        else if (atomic_load(&link_ports_open)) snprintf(out, size, T("Code de la partie : %s  (ouvre le mode multijoueur du jeu)"), code);
        else if (atomic_load(&helper_done)) snprintf(out, size, T("Code de la partie : %s  (ouvre les ports TCP 27312 et 27313 sur ta box)"), code);
        else snprintf(out, size, T("Recherche de l'adresse publique..."));
        break;
    }
    case NET_HOSTING: {
        char code[12];
        if (boot_start && !boot_started && !internet)
            snprintf(out, size, T("En attente du joueur 2 sur le Wi-Fi : le jeu d\xC3\xA9marre \xC3\xA0 son arriv\xC3\xA9" "e (Solo pour jouer seul)"));
        else if (boot_start && !boot_started && netplay_code(code))
            snprintf(out, size, T("Code de la partie : %s  (le jeu d\xC3\xA9marre \xC3\xA0 l'arriv\xC3\xA9" "e du joueur 2)"), code);
        else if (boot_start && boot_started)
            snprintf(out, size, T("Multijoueur : l'autre joueur est parti (rejoindre demande de relancer la partie)"));
        else if (!internet) snprintf(out, size, T("Multijoueur : en attente des joueurs sur le Wi-Fi (jusqu'\xC3\xA0 4)"));
        else if (netplay_code(code)) snprintf(out, size, T("Code de la partie : %s  (en attente des joueurs)"), code);
        else if (atomic_load(&helper_done) && stun_tries >= 8)
            snprintf(out, size, T("Adresse publique introuvable : ouvre le port UDP 47617 sur ta box"));
        else snprintf(out, size, T("Recherche de l'adresse publique..."));
        break;
    }
    case NET_SENDING:
        if (boot_start) snprintf(out, size, T("Arriv\xC3\xA9" "e du joueur %d : d\xC3\xA9part ensemble..."), sending_slot + 1);
        else snprintf(out, size, resyncing ? T("Resynchronisation du joueur %d...") : T("Arriv\xC3\xA9" "e du joueur %d : envoi de la partie..."),
                 sending_slot + 1);
        break;
    case NET_JOINING: snprintf(out, size, T("Connexion \xC3\xA0 l'h\xC3\xB4te...")); break;
    case NET_RECEIVING:
        snprintf(out, size, T("R\xC3\xA9" "ception de la partie : %u %%"), chunk_count ? chunks_received * 100 / chunk_count : 0);
        break;
    case NET_PLAYING: {
        char code[12], extra[48] = "";
        if (hosting && internet && players < NET_MAX_PLAYERS && netplay_code(code)) snprintf(extra, sizeof extra, "  -  code %s", code);
        if (!hosting && atomic_load(&host_busy)) snprintf(out, size, T("Un joueur rejoint la partie..."));
        else if (d) snprintf(out, size, T("Multijoueur : %d joueurs (d\xC3\xA9synchronisation d\xC3\xA9tect\xC3\xA9" "e x%d)%s"), players, d, extra);
        else if (atomic_load(&resyncs))
            snprintf(out, size, T("Multijoueur : %d joueurs (resynchronis\xC3\xA9 x%d)%s"), players, atomic_load(&resyncs), extra);
        else if (hosting) snprintf(out, size, T("Multijoueur : %d joueurs connect\xC3\xA9s%s"), players, extra);
        else snprintf(out, size, T("Multijoueur : %d joueurs, tu es le joueur %d"), players, my_slot + 1);
        break;
    }
    case NET_STALLED: snprintf(out, size, T("Connexion lente ou perdue, en attente d'un joueur...")); break;
    case NET_FAILED: snprintf(out, size, "%s", fail_reason); break;
    default: out[0] = 0;
    }
}

bool netplay_code(char out[12]) {
    pthread_mutex_lock(&public_lock);
    uint32_t ip = public_ip;
    uint16_t port = public_port;
    pthread_mutex_unlock(&public_lock);
    if (!internet || !ip) return false;
    tremu_code_encode(ip, port, out);
    return true;
}

/* ---- Party code lookup (XR thread): asks the host which game it plays ---- */

static int query_sock = -1;
static struct sockaddr_in query_to;
static uint64_t query_started_ns, query_last_ns;

bool netplay_query_start(uint32_t ip, uint16_t port) {
    if (query_sock >= 0) close(query_sock);
    query_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (query_sock < 0) return false;
    fcntl(query_sock, F_SETFL, fcntl(query_sock, F_GETFL) | O_NONBLOCK);
    query_to = (struct sockaddr_in){.sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = htonl(ip)};
    query_started_ns = now_ns();
    query_last_ns = 0;
    return true;
}

int netplay_query_poll(char *key, size_t key_size, char *title, size_t title_size) {
    if (query_sock < 0) return -1;
    uint64_t now = now_ns();
    if (now - query_last_ns > 300 * MS) {
        uint8_t q[4];
        header(q, T_QUERY);
        sendto(query_sock, q, sizeof q, 0, (struct sockaddr *)&query_to, sizeof query_to);
        query_last_ns = now;
    }
    uint8_t p[256];
    ssize_t n = recv(query_sock, p, sizeof p - 1, 0);
    if (n > 5 && p[0] == 'W' && p[1] == 'F' && p[2] == T_INFO) {
        p[n] = 0;
        const char *k = (const char *)p + 4, *t = k + strlen(k) + 1;
        snprintf(key, key_size, "%s", k);
        snprintf(title, title_size, "%s", t < (const char *)p + n ? t : k);
        close(query_sock);
        query_sock = -1;
        return 1;
    }
    if (now - query_started_ns > 8000 * MS) {
        close(query_sock);
        query_sock = -1;
        return -1;
    }
    return 0;
}

/* ---- Discovery (XR thread) ---- */

enum { MAX_HOSTS = 8 };
static int discovery_sock = -1;
static NetHost hosts[MAX_HOSTS];
static int host_count;
static pthread_mutex_t hosts_lock = PTHREAD_MUTEX_INITIALIZER;

static bool is_own_address(struct in_addr a) {
    struct ifaddrs *list, *it;
    bool own = false;
    if (getifaddrs(&list)) return false;
    for (it = list; it && !own; it = it->ifa_next)
        if (it->ifa_addr && it->ifa_addr->sa_family == AF_INET &&
            ((struct sockaddr_in *)it->ifa_addr)->sin_addr.s_addr == a.s_addr)
            own = true;
    freeifaddrs(list);
    return own;
}

void netplay_discovery_poll(uint64_t now) {
    if (discovery_sock < 0) {
        discovery_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (discovery_sock < 0) return;
        int one = 1;
        setsockopt(discovery_sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        setsockopt(discovery_sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
        struct sockaddr_in any = {.sin_family = AF_INET, .sin_port = htons(NET_DISCOVERY_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
        if (bind(discovery_sock, (struct sockaddr *)&any, sizeof any) < 0) {
            close(discovery_sock);
            discovery_sock = -1;
            return;
        }
        fcntl(discovery_sock, F_SETFL, fcntl(discovery_sock, F_GETFL) | O_NONBLOCK);
    }
    uint8_t p[256];
    struct sockaddr_in from;
    socklen_t len;
    ssize_t n;
    pthread_mutex_lock(&hosts_lock);
    while ((len = sizeof from, n = recvfrom(discovery_sock, p, sizeof p - 1, 0, (struct sockaddr *)&from, &len)) > 4) {
        if (p[0] != 'W' || p[1] != 'F' || p[2] != T_ANNOUNCE || p[3] != PROTOCOL || is_own_address(from.sin_addr)) continue;
        p[n] = 0;
        const char *key = (const char *)p + 4, *title = key + strlen(key) + 1;
        if (title >= (const char *)p + n) title = key;
        char ip[64];
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        int i = 0;
        while (i < host_count && strcmp(hosts[i].ip, ip)) i++;
        if (i == MAX_HOSTS) continue;
        if (i == host_count) host_count++;
        snprintf(hosts[i].ip, sizeof hosts[i].ip, "%s", ip);
        snprintf(hosts[i].rom_key, sizeof hosts[i].rom_key, "%s", key);
        snprintf(hosts[i].title, sizeof hosts[i].title, "%s", title);
        hosts[i].seen_ns = now;
    }
    /* Forget hosts silent for 4 s. */
    for (int i = 0; i < host_count;)
        if (now - hosts[i].seen_ns > 4000 * MS) hosts[i] = hosts[--host_count];
        else i++;
    pthread_mutex_unlock(&hosts_lock);
}

int netplay_discovered(NetHost *out, int max) {
    pthread_mutex_lock(&hosts_lock);
    int n = host_count < max ? host_count : max;
    memcpy(out, hosts, n * sizeof *out);
    pthread_mutex_unlock(&hosts_lock);
    return n;
}

uint32_t netplay_file_crc(const char *path) {
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
            table[i] = c;
        }
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t buf[65536];
    uint32_t crc = 0xFFFFFFFFu;
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)))
        for (size_t i = 0; i < n; i++) crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    fclose(f);
    return ~crc;
}
