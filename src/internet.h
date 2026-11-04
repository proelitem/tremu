#pragma once
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Serverless Internet play: no relay, no service paid by anyone.
 * - Party code: the host's public IPv4 and UDP port, 10 Crockford base32 characters.
 * - STUN (free public servers) tells the host its public address.
 * - UPnP asks the host's router to open the game port. */

enum { TREMU_CODE_LENGTH = 10 };
extern const char TREMU_CODE_ALPHABET[33];

/* ip and port in host byte order. out gets "XXXXX-XXXXX". */
void tremu_code_encode(uint32_t ip, uint16_t port, char out[12]);
/* Accepts dashes, spaces and lowercase; O→0 and I/L→1. False on a malformed code. */
bool tremu_code_decode(const char *code, uint32_t *ip, uint16_t *port);

/* Resolves the public STUN servers (blocking DNS). */
int stun_servers(struct sockaddr_in *out, int max);
/* Builds a Binding Request; txid is filled. Returns its length. */
size_t stun_request(uint8_t buf[20], uint8_t txid[12]);
/* True if buf is a Binding Success Response for txid; ip and port (host order) get the mapped address. */
bool stun_parse(const uint8_t *buf, size_t n, const uint8_t txid[12], uint32_t *ip, uint16_t *port);

/* Maps external UDP port → this device's port on the router (blocking, a few seconds at most).
 * On success *external_ip (host order) is the router's public address. */
bool upnp_map_udp(uint16_t port, uint32_t *external_ip);
void upnp_unmap_udp(uint16_t port);
/* Same on TCP, on the router upnp_map_udp found. */
bool upnp_map_tcp(uint16_t port);
void upnp_unmap_tcp(uint16_t port);
