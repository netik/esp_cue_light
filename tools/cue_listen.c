/*
 * cue_listen — join the cue-light LAN via mDNS and print state changes.
 *
 * Discovers _cuelight._tcp peers (same as the ESP8266 boards), polls
 * GET /api/cues on each, and merges updates by sequence number.
 *
 * Build (macOS):
 *   make
 *
 * Build (Linux, Avahi compat):
 *   sudo apt install libavahi-compat-libdnssd-dev
 *   make
 *
 * Usage:
 *   ./cue_listen
 *   ./cue_listen -s 1 -g 1     optional filter (default: all peers)
 *   ./cue_listen -i 100        poll interval in ms (default: 100)
 */

#include <arpa/inet.h>
#include <dns_sd.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define SERVICE_TYPE "_cuelight._tcp."
#define SERVICE_DOMAIN "local."
#define API_PATH "/api/cues"
#define RESPONSE_MAX 512
#define POLL_MS_DEFAULT 100
#define HTTP_TIMEOUT_MS 400
#define MAX_PEERS 16
#define MAX_SD_REFS 32

typedef struct {
  int in_use;
  char name[64];
  char host[256];
  uint16_t port;
  struct sockaddr_storage addr;
  socklen_t addr_len;
  int has_addr;
  int txt_system_id;
  int txt_cue_group;
  int has_txt_system_id;
  int has_txt_cue_group;
} Peer;

typedef struct {
  int valid;
  int system_id;
  int cue_group;
  int cue1;
  int cue2;
  unsigned long seq1;
  unsigned long seq2;
} CueState;

typedef struct {
  DNSServiceRef ref;
} SdRefSlot;

static Peer g_peers[MAX_PEERS];
static SdRefSlot g_sd_refs[MAX_SD_REFS];
static int g_sd_ref_count = 0;
static DNSServiceRef g_browse_ref = NULL;
static int g_filter_system_id = 0;
static int g_filter_cue_group = 0;
static int g_poll_ms = POLL_MS_DEFAULT;
static CueState g_network = {0};

static int add_sd_ref(DNSServiceRef ref) {
  if (g_sd_ref_count >= MAX_SD_REFS) {
    return 0;
  }
  g_sd_refs[g_sd_ref_count++].ref = ref;
  return 1;
}

static void remove_sd_ref(DNSServiceRef ref) {
  for (int i = 0; i < g_sd_ref_count; ++i) {
    if (g_sd_refs[i].ref == ref) {
      g_sd_refs[i] = g_sd_refs[g_sd_ref_count - 1];
      --g_sd_ref_count;
      return;
    }
  }
}

static Peer *alloc_peer(const char *name) {
  for (int i = 0; i < MAX_PEERS; ++i) {
    if (g_peers[i].in_use && strcmp(g_peers[i].name, name) == 0) {
      return &g_peers[i];
    }
  }
  for (int i = 0; i < MAX_PEERS; ++i) {
    if (!g_peers[i].in_use) {
      memset(&g_peers[i], 0, sizeof(g_peers[i]));
      g_peers[i].in_use = 1;
      snprintf(g_peers[i].name, sizeof(g_peers[i].name), "%s", name);
      return &g_peers[i];
    }
  }
  return NULL;
}

static void drop_peer(const char *name) {
  for (int i = 0; i < MAX_PEERS; ++i) {
    if (g_peers[i].in_use && strcmp(g_peers[i].name, name) == 0) {
      g_peers[i].in_use = 0;
      printf("peer left: %s\n", name);
      fflush(stdout);
      return;
    }
  }
}

static int peer_matches_filter(const Peer *peer) {
  if (g_filter_system_id != 0 && peer->has_txt_system_id &&
      peer->txt_system_id != g_filter_system_id) {
    return 0;
  }
  if (g_filter_cue_group != 0 && peer->has_txt_cue_group &&
      peer->txt_cue_group != g_filter_cue_group) {
    return 0;
  }
  return 1;
}

static int parse_uint_json(const char *json, const char *key, unsigned long *out) {
  char pattern[32];
  const char *p;
  char *end;

  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  p = strstr(json, pattern);
  if (p == NULL) {
    return 0;
  }
  p += strlen(pattern);
  *out = strtoul(p, &end, 10);
  return end != p;
}

static int parse_state_json(const char *json, CueState *state) {
  unsigned long v;

  memset(state, 0, sizeof(*state));
  if (!parse_uint_json(json, "system_id", &v)) {
    return 0;
  }
  state->system_id = (int)v;
  if (!parse_uint_json(json, "cue_group", &v)) {
    return 0;
  }
  state->cue_group = (int)v;
  if (!parse_uint_json(json, "cue1", &v)) {
    return 0;
  }
  state->cue1 = (int)v;
  if (!parse_uint_json(json, "cue2", &v)) {
    return 0;
  }
  state->cue2 = (int)v;
  if (!parse_uint_json(json, "seq1", &state->seq1)) {
    return 0;
  }
  if (!parse_uint_json(json, "seq2", &state->seq2)) {
    return 0;
  }
  state->valid = 1;
  return 1;
}

static int is_seq_newer(unsigned long incoming, unsigned long current) {
  return incoming != current && (incoming - current) < 0x80000000UL;
}

static const char *color_name(int cue) {
  return cue ? "GREEN" : "RED";
}

static int resolve_peer_addr(Peer *peer) {
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  char port_str[8];

  peer->has_addr = 0;
  peer->addr_len = 0;

  snprintf(port_str, sizeof(port_str), "%u", peer->port);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(peer->host, port_str, &hints, &res) != 0 || res == NULL) {
    return 0;
  }

  if ((size_t)res->ai_addrlen > sizeof(peer->addr)) {
    freeaddrinfo(res);
    return 0;
  }

  memcpy(&peer->addr, res->ai_addr, res->ai_addrlen);
  peer->addr_len = (socklen_t)res->ai_addrlen;
  peer->has_addr = 1;
  freeaddrinfo(res);
  return 1;
}

static int fetch_cues(const Peer *peer, char *body, size_t body_size) {
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  struct addrinfo *rp;
  int sock = -1;
  char request[320];
  char response[RESPONSE_MAX];
  char port_str[8];
  ssize_t n;
  int status = 0;
  const char *body_start;
  const int timeout_ms = HTTP_TIMEOUT_MS;
  struct timeval tv = {
      .tv_sec = timeout_ms / 1000,
      .tv_usec = (timeout_ms % 1000) * 1000,
  };

  if (peer->has_addr) {
    sock = socket(peer->addr.ss_family, SOCK_STREAM, 0);
    if (sock >= 0) {
      setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
      if (connect(sock, (const struct sockaddr *)&peer->addr, peer->addr_len) !=
          0) {
        close(sock);
        sock = -1;
      }
    }
  }

  if (sock < 0) {
    snprintf(port_str, sizeof(port_str), "%u", peer->port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(peer->host, port_str, &hints, &res) != 0) {
      return 0;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
      sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (sock < 0) {
        continue;
      }

      setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

      if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
        break;
      }
      close(sock);
      sock = -1;
    }
    freeaddrinfo(res);
  }

  if (sock < 0) {
    return 0;
  }

  snprintf(request, sizeof(request),
           "GET " API_PATH " HTTP/1.0\r\n"
           "Host: %s\r\n"
           "Connection: close\r\n"
           "\r\n",
           peer->host);

  if (send(sock, request, strlen(request), 0) < 0) {
    close(sock);
    return 0;
  }

  n = recv(sock, response, sizeof(response) - 1, 0);
  close(sock);
  if (n <= 0) {
    return 0;
  }
  response[n] = '\0';

  if (sscanf(response, "HTTP/%*d.%*d %d", &status) != 1 || status != 200) {
    return 0;
  }

  body_start = strstr(response, "\r\n\r\n");
  if (body_start == NULL) {
    return 0;
  }
  snprintf(body, body_size, "%s", body_start + 4);
  return 1;
}

static void apply_txt(Peer *peer, uint16_t txt_len, const unsigned char *txt) {
  char buf[16];
  const void *value;
  uint8_t len;

  if (TXTRecordContainsKey(txt_len, txt, "system_id")) {
    value = TXTRecordGetValuePtr(txt_len, txt, "system_id", &len);
    if (value != NULL && len < sizeof(buf)) {
      memcpy(buf, value, len);
      buf[len] = '\0';
      peer->txt_system_id = atoi(buf);
      peer->has_txt_system_id = 1;
    }
  }
  if (TXTRecordContainsKey(txt_len, txt, "cue_group")) {
    value = TXTRecordGetValuePtr(txt_len, txt, "cue_group", &len);
    if (value != NULL && len < sizeof(buf)) {
      memcpy(buf, value, len);
      buf[len] = '\0';
      peer->txt_cue_group = atoi(buf);
      peer->has_txt_cue_group = 1;
    }
  }
}

static void print_network_change(const CueState *prev, const CueState *cur) {
  if (!prev->valid) {
    printf("network system=%d group=%d  cue1=%s seq=%lu  cue2=%s seq=%lu\n",
           cur->system_id, cur->cue_group, color_name(cur->cue1), cur->seq1,
           color_name(cur->cue2), cur->seq2);
    return;
  }
  if (cur->cue1 != prev->cue1 || cur->seq1 != prev->seq1) {
    printf("cue1 -> %s (seq %lu -> %lu)\n", color_name(cur->cue1), prev->seq1,
           cur->seq1);
  }
  if (cur->cue2 != prev->cue2 || cur->seq2 != prev->seq2) {
    printf("cue2 -> %s (seq %lu -> %lu)\n", color_name(cur->cue2), prev->seq2,
           cur->seq2);
  }
}

static void merge_peer_state(const CueState *incoming) {
  CueState prev = g_network;
  int changed = 0;

  if (!incoming->valid) {
    return;
  }

  if (g_filter_system_id != 0 && incoming->system_id != g_filter_system_id) {
    return;
  }
  if (g_filter_cue_group != 0 && incoming->cue_group != g_filter_cue_group) {
    return;
  }

  if (!g_network.valid) {
    g_network = *incoming;
    print_network_change(&prev, &g_network);
    fflush(stdout);
    return;
  }

  if (is_seq_newer(incoming->seq1, g_network.seq1)) {
    g_network.cue1 = incoming->cue1;
    g_network.seq1 = incoming->seq1;
    changed = 1;
  }
  if (is_seq_newer(incoming->seq2, g_network.seq2)) {
    g_network.cue2 = incoming->cue2;
    g_network.seq2 = incoming->seq2;
    changed = 1;
  }

  g_network.system_id = incoming->system_id;
  g_network.cue_group = incoming->cue_group;

  if (changed) {
    print_network_change(&prev, &g_network);
    fflush(stdout);
  }
}

static void poll_peer(Peer *peer) {
  char body[256];
  CueState remote;

  if (!peer->in_use || peer->host[0] == '\0') {
    return;
  }
  if (!peer_matches_filter(peer)) {
    return;
  }
  if (fetch_cues(peer, body, sizeof(body)) && parse_state_json(body, &remote)) {
    merge_peer_state(&remote);
  }
}

static void poll_peers(void) {
  for (int i = 0; i < MAX_PEERS; ++i) {
    poll_peer(&g_peers[i]);
  }
}

static void DNSSD_API resolve_reply(DNSServiceRef sd_ref, DNSServiceFlags flags,
                                    uint32_t interface_index,
                                    DNSServiceErrorType error_code,
                                    const char *full_name, const char *host,
                                    uint16_t port, uint16_t txt_len,
                                    const unsigned char *txt_record,
                                    void *context) {
  Peer *peer = (Peer *)context;

  (void)flags;
  (void)interface_index;
  (void)full_name;

  if (error_code != kDNSServiceErr_NoError || peer == NULL) {
    if (peer != NULL) {
      fprintf(stderr, "resolve failed for %s: error %d\n", peer->name,
              (int)error_code);
    }
    goto done;
  }

  snprintf(peer->host, sizeof(peer->host), "%s", host);
  peer->port = ntohs(port);
  apply_txt(peer, txt_len, txt_record);
  resolve_peer_addr(peer);

  if (peer_matches_filter(peer)) {
    printf("peer joined: %s at %s:%u", peer->name, peer->host, peer->port);
    if (peer->has_txt_system_id || peer->has_txt_cue_group) {
      printf(" (system=%d group=%d",
             peer->has_txt_system_id ? peer->txt_system_id : 0,
             peer->has_txt_cue_group ? peer->txt_cue_group : 0);
      if (!peer->has_txt_system_id || !peer->has_txt_cue_group) {
        printf(", txt partial");
      }
      printf(")");
    }
    printf("\n");
    fflush(stdout);
    poll_peer(peer);
  }

done:
  DNSServiceRefDeallocate(sd_ref);
  remove_sd_ref(sd_ref);
}

static void start_resolve(uint32_t interface_index, const char *name,
                          const char *regtype, const char *domain) {
  Peer *peer = alloc_peer(name);
  DNSServiceRef resolve_ref = NULL;
  DNSServiceErrorType err;

  if (peer == NULL) {
    return;
  }

  err = DNSServiceResolve(&resolve_ref, 0, interface_index, name, regtype,
                          domain, resolve_reply, peer);
  if (err != kDNSServiceErr_NoError) {
    fprintf(stderr, "DNSServiceResolve failed: %d\n", (int)err);
    return;
  }

  if (!add_sd_ref(resolve_ref)) {
    DNSServiceRefDeallocate(resolve_ref);
  }
}

static void DNSSD_API browse_reply(DNSServiceRef sd_ref, DNSServiceFlags flags,
                                   uint32_t interface_index,
                                   DNSServiceErrorType error_code,
                                   const char *service_name,
                                   const char *regtype, const char *domain,
                                   void *context) {
  (void)sd_ref;
  (void)context;

  if (error_code != kDNSServiceErr_NoError) {
    return;
  }

  if (flags & kDNSServiceFlagsAdd) {
    printf("discovered: %s\n", service_name);
    fflush(stdout);
    start_resolve(interface_index, service_name, regtype, domain);
  } else {
    drop_peer(service_name);
  }
}

static void drain_ref(DNSServiceRef ref) {
  if (ref == NULL) {
    return;
  }

  const int fd = DNSServiceRefSockFD(ref);
  if (fd < 0) {
    return;
  }

  for (;;) {
    fd_set ready;
    struct timeval zero = {0, 0};

    FD_ZERO(&ready);
    FD_SET(fd, &ready);
    if (select(fd + 1, &ready, NULL, NULL, &zero) <= 0) {
      break;
    }
    if (DNSServiceProcessResult(ref) != kDNSServiceErr_NoError) {
      break;
    }
  }
}

static void process_mdns(int timeout_ms) {
  fd_set readfds;
  int max_fd = -1;
  struct timeval tv;
  DNSServiceRef pending[MAX_SD_REFS];
  int pending_count = 0;

  FD_ZERO(&readfds);

  if (g_browse_ref != NULL) {
    const int fd = DNSServiceRefSockFD(g_browse_ref);
    if (fd >= 0) {
      FD_SET(fd, &readfds);
      if (fd > max_fd) {
        max_fd = fd;
      }
    }
  }

  pending_count = g_sd_ref_count;
  for (int i = 0; i < pending_count; ++i) {
    pending[i] = g_sd_refs[i].ref;
    const int fd = DNSServiceRefSockFD(pending[i]);
    if (fd >= 0) {
      FD_SET(fd, &readfds);
      if (fd > max_fd) {
        max_fd = fd;
      }
    }
  }

  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  if (max_fd >= 0) {
    select(max_fd + 1, &readfds, NULL, NULL, &tv);
  } else if (timeout_ms > 0) {
    usleep((useconds_t)timeout_ms * 1000U);
    return;
  }

  if (g_browse_ref != NULL) {
    const int fd = DNSServiceRefSockFD(g_browse_ref);
    if (fd >= 0 && FD_ISSET(fd, &readfds)) {
      drain_ref(g_browse_ref);
    }
  }

  for (int i = 0; i < pending_count; ++i) {
    const int fd = DNSServiceRefSockFD(pending[i]);
    if (fd >= 0 && FD_ISSET(fd, &readfds)) {
      drain_ref(pending[i]);
    }
  }
}

static long now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long)(tv.tv_sec * 1000L + tv.tv_usec / 1000L);
}

static void usage(const char *prog) {
  fprintf(stderr, "Usage: %s [-s system_id] [-g cue_group] [-i poll_ms]\n", prog);
  fprintf(stderr, "  Browse _cuelight._tcp and print merged cue changes.\n");
  fprintf(stderr, "  Default poll interval: %d ms\n", POLL_MS_DEFAULT);
}

int main(int argc, char **argv) {
  DNSServiceErrorType err;
  long next_poll_ms = 0;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
      g_filter_system_id = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
      g_filter_cue_group = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
      g_poll_ms = atoi(argv[++i]);
      if (g_poll_ms < 20) {
        g_poll_ms = 20;
      }
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  err = DNSServiceBrowse(&g_browse_ref, 0, kDNSServiceInterfaceIndexAny,
                         SERVICE_TYPE, SERVICE_DOMAIN, browse_reply, NULL);
  if (err != kDNSServiceErr_NoError) {
    fprintf(stderr, "DNSServiceBrowse failed: %d\n", (int)err);
    return 1;
  }

  printf("Browsing for %s%s (Ctrl-C to stop)\n", SERVICE_TYPE, SERVICE_DOMAIN);
  if (g_filter_system_id || g_filter_cue_group) {
    printf("Filter: system_id=%d cue_group=%d\n", g_filter_system_id,
           g_filter_cue_group);
  }
  fflush(stdout);

  /* Pick up services already on the network (same as dns-sd -B). */
  drain_ref(g_browse_ref);

  next_poll_ms = now_ms();

  for (;;) {
    const long now = now_ms();
    const int until_poll =
        (now >= next_poll_ms) ? 0 : (int)(next_poll_ms - now);
    const int mdns_cap = (g_poll_ms > 50) ? 50 : g_poll_ms;
    const int wait_ms = (until_poll > mdns_cap) ? mdns_cap : until_poll;

    process_mdns(wait_ms > 0 ? wait_ms : 0);

    if (now_ms() >= next_poll_ms) {
      poll_peers();
      next_poll_ms = now_ms() + g_poll_ms;
    }
  }

  return 0;
}
