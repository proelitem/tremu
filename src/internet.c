#include "internet.h"
#include <android/log.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu-net", __VA_ARGS__)

/* ---- Party code ---- */

const char WFG_CODE_ALPHABET[33] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  /* Crockford base32 */

/* 48 bits of address and port, plus 2 check bits catching most typos: 50 bits, 10 symbols. */
static unsigned check_bits(uint64_t value) {
    unsigned sum = 0;
    for (int i = 0; i < 6; i++) sum += (unsigned)(value >> (8 * i)) & 0xFF;
    return sum & 3;
}

void wfg_code_encode(uint32_t ip, uint16_t port, char out[12]) {
    uint64_t value = (uint64_t)ip << 16 | port;
    uint64_t bits = value << 2 | check_bits(value);
    int k = 0;
    for (int i = 0; i < WFG_CODE_LENGTH; i++) {
        if (i == 5) out[k++] = '-';
        out[k++] = WFG_CODE_ALPHABET[(bits >> (45 - 5 * i)) & 31];
    }
    out[k] = 0;
}

bool wfg_code_decode(const char *code, uint32_t *ip, uint16_t *port) {
    uint64_t bits = 0;
    int count = 0;
    for (const char *c = code; *c; c++) {
        char ch = (char)toupper((unsigned char)*c);
        if (ch == '-' || ch == ' ') continue;
        if (ch == 'O') ch = '0';
        if (ch == 'I' || ch == 'L') ch = '1';
        const char *at = strchr(WFG_CODE_ALPHABET, ch);
        if (!at || !*at || count == WFG_CODE_LENGTH) return false;
        bits = bits << 5 | (uint64_t)(at - WFG_CODE_ALPHABET);
        count++;
    }
    if (count != WFG_CODE_LENGTH) return false;
    uint64_t value = bits >> 2;
    if ((bits & 3) != check_bits(value)) return false;
    *ip = (uint32_t)(value >> 16);
    *port = (uint16_t)value;
    return *ip && *port;
}

/* ---- STUN (RFC 5389) ---- */

enum { STUN_COOKIE = 0x2112A442 };

int stun_servers(struct sockaddr_in *out, int max) {
    /* Free public servers; several, since none of them promises to stay. */
    static const struct { const char *host; uint16_t port; } list[] = {
        {"stun.cloudflare.com", 3478}, {"stun.l.google.com", 19302}, {"stun1.l.google.com", 19302},
        {"global.stun.twilio.com", 3478},
    };
    int n = 0;
    for (size_t i = 0; i < sizeof list / sizeof list[0] && n < max; i++) {
        struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM}, *res;
        if (getaddrinfo(list[i].host, NULL, &hints, &res)) continue;
        out[n] = *(struct sockaddr_in *)res->ai_addr;
        out[n].sin_port = htons(list[i].port);
        n++;
        freeaddrinfo(res);
    }
    LOG("%d STUN servers resolved", n);
    return n;
}

size_t stun_request(uint8_t buf[20], uint8_t txid[12]) {
    for (int i = 0; i < 12; i++) txid[i] = (uint8_t)(rand() ^ (int)time(NULL) >> (i % 4));
    buf[0] = 0x00; buf[1] = 0x01;  /* Binding Request */
    buf[2] = 0x00; buf[3] = 0x00;  /* no attributes */
    buf[4] = 0x21; buf[5] = 0x12; buf[6] = 0xA4; buf[7] = 0x42;
    memcpy(buf + 8, txid, 12);
    return 20;
}

bool stun_parse(const uint8_t *b, size_t n, const uint8_t txid[12], uint32_t *ip, uint16_t *port) {
    if (n < 20 || b[0] != 0x01 || b[1] != 0x01 || memcmp(b + 8, txid, 12)) return false;
    size_t len = (size_t)b[2] << 8 | b[3], at = 20;
    bool found = false;
    while (at + 4 <= 20 + len && at + 4 <= n) {
        unsigned type = b[at] << 8 | b[at + 1], alen = b[at + 2] << 8 | b[at + 3];
        const uint8_t *v = b + at + 4;
        if (at + 4 + alen > n) break;
        if ((type == 0x0020 || type == 0x0001) && alen >= 8 && v[1] == 0x01) {
            uint16_t p = (uint16_t)(v[2] << 8 | v[3]);
            uint32_t a = (uint32_t)v[4] << 24 | v[5] << 16 | v[6] << 8 | v[7];
            if (type == 0x0020) { p ^= STUN_COOKIE >> 16; a ^= STUN_COOKIE; }
            *ip = a;
            *port = p;
            found = true;
            if (type == 0x0020) break;  /* prefer XOR-MAPPED-ADDRESS */
        }
        at += 4 + ((alen + 3) & ~3u);
    }
    return found;
}

/* ---- UPnP Internet Gateway Device ---- */

static char control_url[512], service_type[96];
static char local_ip[64];

static void set_timeouts(int s, int ms) {
    struct timeval tv = {ms / 1000, (ms % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

/* Splits http://host:port/path. */
static bool parse_url(const char *url, char *host, size_t host_size, int *port, char *path, size_t path_size) {
    if (strncmp(url, "http://", 7)) return false;
    const char *h = url + 7, *slash = strchr(h, '/'), *colon = strchr(h, ':');
    if (!slash) slash = h + strlen(h);
    if (colon && colon < slash) {
        snprintf(host, host_size, "%.*s", (int)(colon - h), h);
        *port = atoi(colon + 1);
    } else {
        snprintf(host, host_size, "%.*s", (int)(slash - h), h);
        *port = 80;
    }
    snprintf(path, path_size, "%s", *slash ? slash : "/");
    return true;
}

/* Minimal HTTP/1.0 exchange; returns the whole response in a malloc'd string. */
static char *http(const char *host, int port, const char *request) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return NULL;
    set_timeouts(s, 2000);
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
    inet_pton(AF_INET, host, &to.sin_addr);
    if (connect(s, (struct sockaddr *)&to, sizeof to) || send(s, request, strlen(request), 0) < 0) {
        close(s);
        return NULL;
    }
    size_t cap = 16384, len = 0;
    char *out = malloc(cap);
    ssize_t n;
    while (out && (n = recv(s, out + len, cap - len - 1, 0)) > 0) {
        len += (size_t)n;
        if (len + 1 >= cap) {
            char *bigger = realloc(out, cap *= 2);
            if (!bigger) { free(out); out = NULL; }
            else out = bigger;
        }
    }
    close(s);
    if (out) out[len] = 0;
    return out;
}

static bool soap(const char *action, const char *args, char **response) {
    char host[64], path[256];
    int port;
    if (!parse_url(control_url, host, sizeof host, &port, path, sizeof path)) return false;
    char body[2048], request[3072];
    snprintf(body, sizeof body,
             "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%s xmlns:u=\"%s\">%s</u:%s>"
             "</s:Body></s:Envelope>",
             action, service_type, args, action);
    snprintf(request, sizeof request,
             "POST %s HTTP/1.0\r\nHost: %s:%d\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
             "SOAPAction: \"%s#%s\"\r\nContent-Length: %zu\r\n\r\n%s",
             path, host, port, service_type, action, strlen(body), body);
    char *reply = http(host, port, request);
    bool ok = reply && strstr(reply, " 200 ");
    if (response) *response = reply;
    else free(reply);
    return ok;
}

/* Finds the router with SSDP and its WAN connection service. */
static bool discover_gateway(void) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return false;
    set_timeouts(s, 2500);
    struct sockaddr_in group = {.sin_family = AF_INET, .sin_port = htons(1900)};
    inet_pton(AF_INET, "239.255.255.250", &group.sin_addr);
    /* The local address on the route to the router is the one to map to. A separate socket finds
     * it: a connected socket would drop the router's replies, which come from its own address. */
    int probe = socket(AF_INET, SOCK_DGRAM, 0);
    if (probe >= 0 && !connect(probe, (struct sockaddr *)&group, sizeof group)) {
        struct sockaddr_in me;
        socklen_t me_len = sizeof me;
        getsockname(probe, (struct sockaddr *)&me, &me_len);
        inet_ntop(AF_INET, &me.sin_addr, local_ip, sizeof local_ip);
    }
    if (probe >= 0) close(probe);
    for (int v = 1; v <= 2; v++) {
        char search[256];
        snprintf(search, sizeof search,
                 "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
                 "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:%d\r\n\r\n", v);
        sendto(s, search, strlen(search), 0, (struct sockaddr *)&group, sizeof group);
    }
    char reply[2048], location[512] = "";
    ssize_t n;
    while (!location[0] && (n = recv(s, reply, sizeof reply - 1, 0)) > 0) {
        reply[n] = 0;
        for (char *line = reply; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL)
            if (!strncasecmp(line, "LOCATION:", 9)) {
                const char *v = line + 9;
                while (*v == ' ') v++;
                snprintf(location, sizeof location, "%.*s", (int)strcspn(v, "\r\n"), v);
                break;
            }
    }
    close(s);
    if (!location[0]) { LOG("UPnP: no gateway answered"); return false; }

    char host[64], path[256], request[512];
    int port;
    if (!parse_url(location, host, sizeof host, &port, path, sizeof path)) return false;
    snprintf(request, sizeof request, "GET %s HTTP/1.0\r\nHost: %s:%d\r\n\r\n", path, host, port);
    char *xml = http(host, port, request);
    if (!xml) return false;
    static const char *services[] = {"urn:schemas-upnp-org:service:WANIPConnection:2",
                                     "urn:schemas-upnp-org:service:WANIPConnection:1",
                                     "urn:schemas-upnp-org:service:WANPPPConnection:1"};
    bool found = false;
    for (size_t i = 0; i < 3 && !found; i++) {
        const char *at = strstr(xml, services[i]), *ctl = at ? strstr(at, "<controlURL>") : NULL;
        if (!ctl) continue;
        ctl += 12;
        char url[384];
        snprintf(url, sizeof url, "%.*s", (int)strcspn(ctl, "<"), ctl);
        if (!strncmp(url, "http://", 7)) snprintf(control_url, sizeof control_url, "%s", url);
        else snprintf(control_url, sizeof control_url, "http://%s:%d%s%s", host, port, url[0] == '/' ? "" : "/", url);
        snprintf(service_type, sizeof service_type, "%s", services[i]);
        found = true;
    }
    free(xml);
    LOG("UPnP gateway %s, %s", found ? control_url : "without WAN service", found ? service_type : "");
    return found;
}

static bool add_mapping(uint16_t port, const char *protocol) {
    char args[512];
    snprintf(args, sizeof args,
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>%s</NewProtocol>"
             "<NewInternalPort>%u</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
             "<NewPortMappingDescription>Tremu</NewPortMappingDescription><NewLeaseDuration>7200</NewLeaseDuration>",
             port, protocol, port, local_ip);
    if (!soap("AddPortMapping", args, NULL)) { LOG("UPnP: AddPortMapping %s %u refused", protocol, port); return false; }
    return true;
}

static void delete_mapping(uint16_t port, const char *protocol) {
    if (!control_url[0]) return;
    char args[256];
    snprintf(args, sizeof args,
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>%s</NewProtocol>", port, protocol);
    soap("DeletePortMapping", args, NULL);
}

bool upnp_map_udp(uint16_t port, uint32_t *external_ip) {
    if (!discover_gateway() || !local_ip[0]) return false;
    if (!add_mapping(port, "UDP")) return false;
    char *reply = NULL;
    bool ok = soap("GetExternalIPAddress", "", &reply);
    const char *at = ok && reply ? strstr(reply, "<NewExternalIPAddress>") : NULL;
    struct in_addr a;
    ok = false;
    if (at) {
        char ip[64];
        at += 22;
        snprintf(ip, sizeof ip, "%.*s", (int)strcspn(at, "<"), at);
        ok = inet_pton(AF_INET, ip, &a) == 1 && a.s_addr;
        if (ok) *external_ip = ntohl(a.s_addr);
    }
    free(reply);
    LOG("UPnP: port %u mapped to %s, external address %s", port, local_ip, ok ? "known" : "unknown");
    return ok;
}

void upnp_unmap_udp(uint16_t port) { delete_mapping(port, "UDP"); }

bool upnp_map_tcp(uint16_t port) { return local_ip[0] && control_url[0] && add_mapping(port, "TCP"); }

void upnp_unmap_tcp(uint16_t port) { delete_mapping(port, "TCP"); }
