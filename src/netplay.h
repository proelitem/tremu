#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "core.h"

/* Play with up to four headsets: deterministic lockstep with input delay. Every headset runs
 * the same core and ROM; only gun inputs travel, in a star around the host (player 1): guests
 * (players 2 to 4) send their gun to the host, which sends every guest the whole frame. Players
 * can join a running game (it pauses while the newcomer receives the game state). All functions
 * except the discovery ones run on the emulator thread (they own the core state). */

enum { NET_GAME_PORT = 47617, NET_DISCOVERY_PORT = 47616, NET_MAX_PLAYERS = 4 };

typedef enum {
    NET_OFF,
    NET_HOSTING,   /* host: playing alone, announcing, waiting for guests */
    NET_SENDING,   /* host: a guest accepted, transferring the game state (game paused) */
    NET_JOINING,   /* guest: asking the host to join */
    NET_RECEIVING, /* guest: receiving the game state */
    NET_PLAYING,   /* all: lockstep running */
    NET_STALLED,   /* all: a player silent for a while, game frozen */
    NET_FAILED,    /* guest: refused or timed out (see netplay_status) */
    NET_LINK,      /* host of a game with its own network (PSP ad hoc): announced, no lockstep */
} NetState;

/* rom_key is the archive name (e.g. "term2.zip"); rom_crc identifies the exact file.
 * over_internet also finds the public address (UPnP, STUN) and makes a party code. */
bool netplay_host(const char *rom_key, const char *title, uint32_t rom_crc, bool over_internet);
/* Announces a game that links the headsets itself (PSP ad hoc): the others find it like a lockstep
 * game, then their core connects to this headset. Serve it with netplay_idle, the game runs freely. */
bool netplay_host_link(const char *rom_key, const char *title, bool over_internet);
/* This headset's address on the local network; false without a network. */
bool netplay_local_ip(char *out, size_t size);
/* host is "ip" on the local network or "ip:port" from a party code. */
bool netplay_join(const char *host, const char *rom_key, uint32_t rom_crc);
void netplay_stop(void);
NetState netplay_state(void);
bool netplay_is_host(void);
bool netplay_is_internet(void);
/* Players in the game, this headset included (1 when alone). */
int netplay_players(void);
/* Internet host: the party code "XXXXX-XXXXX" once the public address is known. */
bool netplay_code(char out[12]);
/* Forgets a NET_FAILED state once its reason has been shown (any thread). */
void netplay_dismiss_failure(void);
/* One step per emulated frame. local is this headset's gun. Returns true with guns[0..3]
 * filled when a frame may run, false when it must wait (handshake, transfer or missing input). */
bool netplay_frame(const WgGun *local, WgGun guns[NET_MAX_PLAYERS]);
/* Call after each emulated frame (sync checks). */
void netplay_after_frame(void);
/* Call while the game is paused (menu, headset removed): keeps the network serviced
 * (announcements, state transfer, keep-alive) without advancing the game. */
void netplay_idle(void);
/* True while the game state is being transferred (worth polling the network often). */
bool netplay_transferring(void);
/* Short human-readable status, French, for the menu and carousel. */
void netplay_status(char *out, size_t size);

/* Discovery (XR thread): listens for host announcements on the local network. */
typedef struct NetHost {
    char ip[64], rom_key[64], title[64];
    uint64_t seen_ns;
} NetHost;
void netplay_discovery_poll(uint64_t now_ns);
/* Hosts heard in the last few seconds; returns their count. */
int netplay_discovered(NetHost *out, int max);

uint32_t netplay_file_crc(const char *path);

/* Party code lookup (XR thread): asks the host at ip:port (host order) which game it plays.
 * Poll returns 1 with key/title filled, 0 while waiting, -1 after ~8 s without answer. */
bool netplay_query_start(uint32_t ip, uint16_t port);
int netplay_query_poll(char *key, size_t key_size, char *title, size_t title_size);
