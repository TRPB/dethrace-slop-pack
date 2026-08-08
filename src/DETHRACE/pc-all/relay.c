// relay.c - UDP relay client for dethrace internet multiplayer.
// Enabled by RelayServer=ip:port under [Slop] in dethrace.ini.
// See relay.h for the public API and the companion dethrace-relay Rust server
// for the server-side implementation.
//
// Protocol v2 (little-endian, UDP datagrams):
//   Header: "DREL" (4 bytes) + msg_type (u8) + payload_len (u16 LE) + token (u64 LE)
//
// Handshake (token=0 in header):
//   Client: 0x08 HELLO      []
//   Server: 0x8A CHALLENGE  [nonce(u64)]
//   Client: 0x09 RESPOND    [nonce(u64)]  -- echo the nonce back
//   Server: 0x8B WELCOME    [token(u64)]  -- issued credential
//
// Post-auth (token=issued in header):
//   0x01 LIST_SESSIONS  []
//   0x02 HOST_SESSION   host_name[32] + num_players(u8) + game_type(u8) + status(u8)
//   0x03 JOIN_SESSION   session_id(u32)
//   0x04 LEAVE_SESSION  session_id(u32)
//   0x05 GAME_PACKET    session_id(u32) + dest_slot(u8,0xFF=all) + data[]
//   0x06 HEARTBEAT      session_id(u32)
//   0x07 UPDATE_SESSION session_id(u32) + num_players(u8) + status(u8)
//
// Server → Client:
//   0x81 SESSION_LIST    count(u8) + [session_id(u32)+host_name[32]+num_p(u8)+type(u8)+status(u8)]*count
//   0x82 SESSION_CREATED session_id(u32)
//   0x83 SESSION_JOINED  slot(u8)
//   0x84 SESSION_ERROR   error_code(u8)
//   0x85 INCOMING_PACKET session_id(u32) + from_slot(u8) + data[]
//   0x86 PLAYER_JOINED   slot(u8)
//   0x87 PLAYER_LEFT     slot(u8)
//   0x8A CHALLENGE       nonce(u64)
//   0x8B WELCOME         token(u64)
//
// Fake relay addresses (tCopyable_sockaddr_in):
//   .address = 0x52454C4100000000 | session_id   ("RELA" magic in high 4 bytes)
//   .port    = slot  (0xFF = not yet assigned)

#include "relay.h"

#include "errors.h"
#include "harness/config.h"
#include "harness/os.h"
#include "network.h"
#include "pd/sys.h"

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#define RELAY_EWOULDBLOCK WSAEWOULDBLOCK
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#define RELAY_EWOULDBLOCK EWOULDBLOCK
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELAY_MAGIC "DREL"
#define RELAY_HEADER_SIZE 15 // 4 magic + 1 type + 2 len + 8 token
#define RELAY_MAX_PAYLOAD 1024
#define RELAY_MAX_SESSIONS 16
// Maximum broadcast stack size as defined by MAX_MESAGE_STACK_SIZE in network.c.
// Game packets are capped here so we never exceed what tMax_message can hold (512 bytes).
#define RELAY_GAME_MAX_STACK 512
#define RELAY_DISCOVER_TIMEOUT_MS 2000
#define RELAY_JOIN_TIMEOUT_MS 3000
#define RELAY_HANDSHAKE_TIMEOUT_MS 5000

// Message types client → server
#define RMSG_LIST_SESSIONS  0x01
#define RMSG_HOST_SESSION   0x02
#define RMSG_JOIN_SESSION   0x03
#define RMSG_LEAVE_SESSION  0x04
#define RMSG_GAME_PACKET    0x05
#define RMSG_HEARTBEAT      0x06
#define RMSG_UPDATE_SESSION 0x07
#define RMSG_HELLO          0x08
#define RMSG_RESPOND        0x09

// Message types server → client
#define RMSG_SESSION_LIST    0x81
#define RMSG_SESSION_CREATED 0x82
#define RMSG_SESSION_JOINED  0x83
#define RMSG_SESSION_ERROR   0x84
#define RMSG_INCOMING_PACKET 0x85
#define RMSG_PLAYER_JOINED   0x86
#define RMSG_PLAYER_LEFT     0x87
#define RMSG_CHALLENGE       0x8A
#define RMSG_WELCOME         0x8B

#define RELAY_ADDR_MAGIC UINT64_C(0x52454C4100000000)

// State
static int gRelay_socket = -1;
static struct sockaddr_in gRelay_server_addr;
static uint64_t gRelay_token = 0;
static uint32_t gRelay_session_id;
static uint8_t gRelay_slot;

// Cached session list from last LIST_SESSIONS
typedef struct {
    uint32_t session_id;
    char host_name[33];
    int num_players;
    int game_type;
    int status;
} tRelay_session;

static tRelay_session gRelay_sessions[RELAY_MAX_SESSIONS];
static int gRelay_session_count;

// Passed down from allnet for join list population
static tPD_net_game_info* gRelay_joinable;
static int* gRelay_joinable_count_ptr;

// Used as *ppHost_address in Relay_HostGame; must outlive the call.
static tCopyable_sockaddr_in gRelay_host_addr;

// Used as *ppSender_address in Relay_GetNextMessage; must outlive the call.
static tCopyable_sockaddr_in gRelay_sender_addr;

// ---- Helpers ---------------------------------------------------------------

static int relay_addr_is_relay(const tCopyable_sockaddr_in* addr) {
    return (addr->address & UINT64_C(0xFFFFFFFF00000000)) == RELAY_ADDR_MAGIC;
}

static tCopyable_sockaddr_in relay_addr_make(uint32_t session_id, uint8_t slot) {
    tCopyable_sockaddr_in addr;
    addr.address = RELAY_ADDR_MAGIC | (uint64_t)session_id;
    addr.port = slot;
    return addr;
}

static uint32_t relay_addr_session(const tCopyable_sockaddr_in* addr) {
    return (uint32_t)(addr->address & 0xFFFFFFFF);
}

static uint8_t relay_addr_slot(const tCopyable_sockaddr_in* addr) {
    return (uint8_t)addr->port;
}

static int relay_send(uint8_t msg_type, const uint8_t* payload, uint16_t payload_len) {
    uint8_t buf[RELAY_HEADER_SIZE + RELAY_MAX_PAYLOAD];
    if (payload_len > RELAY_MAX_PAYLOAD) {
        return -1;
    }
    memcpy(buf, RELAY_MAGIC, 4);
    buf[4] = msg_type;
    buf[5] = (uint8_t)(payload_len & 0xFF);
    buf[6] = (uint8_t)((payload_len >> 8) & 0xFF);
    // Token: 8 bytes LE
    buf[7]  = (uint8_t)(gRelay_token & 0xFF);
    buf[8]  = (uint8_t)((gRelay_token >> 8) & 0xFF);
    buf[9]  = (uint8_t)((gRelay_token >> 16) & 0xFF);
    buf[10] = (uint8_t)((gRelay_token >> 24) & 0xFF);
    buf[11] = (uint8_t)((gRelay_token >> 32) & 0xFF);
    buf[12] = (uint8_t)((gRelay_token >> 40) & 0xFF);
    buf[13] = (uint8_t)((gRelay_token >> 48) & 0xFF);
    buf[14] = (uint8_t)((gRelay_token >> 56) & 0xFF);
    if (payload_len > 0 && payload != NULL) {
        memcpy(buf + RELAY_HEADER_SIZE, payload, payload_len);
    }
    int res = sendto(gRelay_socket, (const char*)buf, RELAY_HEADER_SIZE + payload_len, 0,
        (struct sockaddr*)&gRelay_server_addr, sizeof(gRelay_server_addr));
    return res < 0 ? -1 : 0;
}

// Blocking receive with timeout. Returns payload length on success, -1 on error/timeout.
static int relay_recv_wait(uint8_t* msg_type_out, uint8_t* payload, int timeout_ms) {
    uint8_t buf[RELAY_HEADER_SIZE + RELAY_MAX_PAYLOAD];
    struct timeval tv;
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(gRelay_socket, &fds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (select(gRelay_socket + 1, &fds, NULL, NULL, &tv) <= 0) {
        return -1;
    }

    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    int len = recvfrom(gRelay_socket, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &from_len);
    if (len < RELAY_HEADER_SIZE) {
        return -1;
    }
    if (memcmp(buf, RELAY_MAGIC, 4) != 0) {
        return -1;
    }
    *msg_type_out = buf[4];
    uint16_t plen = (uint16_t)buf[5] | ((uint16_t)buf[6] << 8);
    if ((int)plen > RELAY_MAX_PAYLOAD || len < RELAY_HEADER_SIZE + (int)plen) {
        return -1;
    }
    if (plen > 0) {
        memcpy(payload, buf + RELAY_HEADER_SIZE, plen);
    }
    return (int)plen;
}

// Non-blocking receive. Returns payload length on success, -1 if nothing waiting.
static int relay_recv_nowait(uint8_t* msg_type_out, uint8_t* payload) {
    uint8_t buf[RELAY_HEADER_SIZE + RELAY_MAX_PAYLOAD];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);

    int len = recvfrom(gRelay_socket, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &from_len);
    if (len < 0) {
        return -1;
    }
    if (len < RELAY_HEADER_SIZE || memcmp(buf, RELAY_MAGIC, 4) != 0) {
        return -1;
    }
    *msg_type_out = buf[4];
    uint16_t plen = (uint16_t)buf[5] | ((uint16_t)buf[6] << 8);
    if ((int)plen > RELAY_MAX_PAYLOAD || len < RELAY_HEADER_SIZE + (int)plen) {
        return -1;
    }
    if (plen > 0) {
        memcpy(payload, buf + RELAY_HEADER_SIZE, plen);
    }
    return (int)plen;
}

// Perform HELLO/CHALLENGE/RESPOND/WELCOME handshake with the relay server.
// Sets gRelay_token on success. Returns 0 on success, -1 on failure.
static int relay_handshake(void) {
    uint8_t msg_type;
    uint8_t resp[RELAY_MAX_PAYLOAD];
    int len;

    relay_send(RMSG_HELLO, NULL, 0);
    len = relay_recv_wait(&msg_type, resp, RELAY_HANDSHAKE_TIMEOUT_MS);
    if (len < 8 || msg_type != RMSG_CHALLENGE) {
        dr_dprintf("relay_handshake: no CHALLENGE response");
        return -1;
    }

    // Echo the nonce back in a RESPOND message
    relay_send(RMSG_RESPOND, resp, 8);
    len = relay_recv_wait(&msg_type, resp, RELAY_HANDSHAKE_TIMEOUT_MS);
    if (len < 8 || msg_type != RMSG_WELCOME) {
        dr_dprintf("relay_handshake: no WELCOME response");
        return -1;
    }

    gRelay_token = (uint64_t)resp[0]
        | ((uint64_t)resp[1] << 8)
        | ((uint64_t)resp[2] << 16)
        | ((uint64_t)resp[3] << 24)
        | ((uint64_t)resp[4] << 32)
        | ((uint64_t)resp[5] << 40)
        | ((uint64_t)resp[6] << 48)
        | ((uint64_t)resp[7] << 56);
    dr_dprintf("relay_handshake: authenticated with relay");
    return 0;
}

// ---- Public API ------------------------------------------------------------

int Relay_IsEnabled(void) {
    return gRelay_socket >= 0;
}

int Relay_Init(void) {
    char host[256];
    int port = 7777;
    const char* server = harness_game_config.relay_server;

    if (server[0] == '\0') {
        return 0;
    }

    const char* colon = strrchr(server, ':');
    if (colon != NULL) {
        size_t host_len = (size_t)(colon - server);
        if (host_len >= sizeof(host)) {
            host_len = sizeof(host) - 1;
        }
        memcpy(host, server, host_len);
        host[host_len] = '\0';
        port = atoi(colon + 1);
    } else {
        strncpy(host, server, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    if (port <= 0 || port > 65535) {
        dr_dprintf("Relay_Init: invalid port in relay_server '%s'", server);
        return -1;
    }

    memset(&gRelay_server_addr, 0, sizeof(gRelay_server_addr));
    gRelay_server_addr.sin_family = AF_INET;
    gRelay_server_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &gRelay_server_addr.sin_addr) != 1) {
        dr_dprintf("Relay_Init: invalid relay server address '%s'", host);
        return -1;
    }

    gRelay_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (gRelay_socket < 0) {
        dr_dprintf("Relay_Init: failed to create socket");
        return -1;
    }

    if (OS_SetSocketNonBlocking(gRelay_socket) < 0) {
        dr_dprintf("Relay_Init: failed to set non-blocking");
        OS_CloseSocket(gRelay_socket);
        gRelay_socket = -1;
        return -1;
    }

    gRelay_session_id = 0;
    gRelay_slot = 0xFF;
    gRelay_token = 0;
    dr_dprintf("Relay_Init: relay server %s:%d", host, port);

    if (relay_handshake() < 0) {
        dr_dprintf("Relay_Init: handshake failed, relay disabled");
        OS_CloseSocket(gRelay_socket);
        gRelay_socket = -1;
        return -1;
    }
    return 0;
}

void Relay_Shutdown(void) {
    if (gRelay_socket >= 0) {
        OS_CloseSocket(gRelay_socket);
        gRelay_socket = -1;
    }
    gRelay_token = 0;
}

void Relay_StartProducingJoinList(tPD_net_game_info* joinable, int* count) {
    uint8_t payload[RELAY_MAX_PAYLOAD];
    uint8_t msg_type;
    int len;

    gRelay_joinable = joinable;
    gRelay_joinable_count_ptr = count;
    *count = 0;
    gRelay_session_count = 0;

    relay_send(RMSG_LIST_SESSIONS, NULL, 0);
    len = relay_recv_wait(&msg_type, payload, RELAY_DISCOVER_TIMEOUT_MS);
    if (len < 1 || msg_type != RMSG_SESSION_LIST) {
        dr_dprintf("Relay_StartProducingJoinList: no response");
        return;
    }

    int n = payload[0];
    if (n > RELAY_MAX_SESSIONS) {
        n = RELAY_MAX_SESSIONS;
    }

    int offset = 1;
    // Each entry: session_id(4) + host_name(32) + num_players(1) + game_type(1) + status(1) = 39 bytes
    for (int i = 0; i < n; i++) {
        if (offset + 39 > len) {
            break;
        }
        tRelay_session* s = &gRelay_sessions[gRelay_session_count];
        s->session_id = (uint32_t)payload[offset]
            | ((uint32_t)payload[offset + 1] << 8)
            | ((uint32_t)payload[offset + 2] << 16)
            | ((uint32_t)payload[offset + 3] << 24);
        offset += 4;
        memcpy(s->host_name, payload + offset, 32);
        s->host_name[32] = '\0';
        offset += 32;
        s->num_players = payload[offset++];
        s->game_type = payload[offset++];
        s->status = payload[offset++];

        if (*count < 16) {
            joinable[*count].addr_in = relay_addr_make(s->session_id, 0);
            joinable[*count].last_response = PDGetTotalTime();
            (*count)++;
        }
        gRelay_session_count++;
    }
    dr_dprintf("Relay_StartProducingJoinList: %d session(s)", gRelay_session_count);
}

int Relay_GetNextJoinGame(tNet_game_details* pGame, int pIndex) {
    if (pIndex >= *gRelay_joinable_count_ptr) {
        return 0;
    }
    pGame->pd_net_info = *(tPD_net_player_info*)&gRelay_joinable[pIndex];
    if (pIndex < gRelay_session_count) {
        tRelay_session* s = &gRelay_sessions[pIndex];
        strncpy(pGame->host_name, s->host_name, sizeof(pGame->host_name) - 1);
        pGame->host_name[sizeof(pGame->host_name) - 1] = '\0';
        pGame->num_players = s->num_players;
        // Clamp game_type to valid enum range before casting
        if (s->game_type >= 0 && s->game_type < eNet_game_type_count) {
            pGame->type = (tNet_game_type)s->game_type;
        } else {
            pGame->type = eNet_game_type_fight_to_death;
        }
        pGame->status.stage = (tNet_game_stage)s->status;
        pGame->no_races_yet = (s->status == 0);
        pGame->options.open_game = 1;
    }
    return 1;
}

int Relay_HostGame(tNet_game_details* pDetails, char* pHost_name, void** ppHost_address) {
    uint8_t payload[35];
    uint8_t resp[RELAY_MAX_PAYLOAD];
    uint8_t msg_type;
    int len;

    memset(payload, 0, sizeof(payload));
    strncpy((char*)payload, pHost_name, 31);
    payload[32] = (uint8_t)pDetails->num_players;
    payload[33] = (uint8_t)pDetails->type;
    payload[34] = (uint8_t)pDetails->status.stage;

    relay_send(RMSG_HOST_SESSION, payload, 35);
    len = relay_recv_wait(&msg_type, resp, RELAY_JOIN_TIMEOUT_MS);
    if (len < 4 || msg_type != RMSG_SESSION_CREATED) {
        dr_dprintf("Relay_HostGame: no response from relay");
        return 0;
    }

    gRelay_session_id = (uint32_t)resp[0]
        | ((uint32_t)resp[1] << 8)
        | ((uint32_t)resp[2] << 16)
        | ((uint32_t)resp[3] << 24);
    gRelay_slot = 0;

    gRelay_host_addr = relay_addr_make(gRelay_session_id, 0);
    *ppHost_address = &gRelay_host_addr;
    dr_dprintf("Relay_HostGame: session_id=%u", (unsigned)gRelay_session_id);
    return 1;
}

int Relay_JoinGame(tNet_game_details* pDetails, char* pPlayer_name) {
    uint8_t payload[4];
    uint8_t resp[RELAY_MAX_PAYLOAD];
    uint8_t msg_type;
    int len;
    uint32_t session_id;

    tCopyable_sockaddr_in* addr = &pDetails->pd_net_info.addr_in;
    if (!relay_addr_is_relay(addr)) {
        dr_dprintf("Relay_JoinGame: address is not a relay address");
        return 1; // non-zero = failure
    }
    session_id = relay_addr_session(addr);

    // Already in this session — the relay would reassign a different slot on a
    // second JOIN_SESSION, breaking the host's slot→address mapping for this player.
    if (gRelay_session_id == session_id && gRelay_slot != 0xFF) {
        dr_dprintf("Relay_JoinGame: already in session %u as slot %d", (unsigned)session_id, (int)gRelay_slot);
        return 0;
    }

    payload[0] = (uint8_t)(session_id & 0xFF);
    payload[1] = (uint8_t)((session_id >> 8) & 0xFF);
    payload[2] = (uint8_t)((session_id >> 16) & 0xFF);
    payload[3] = (uint8_t)((session_id >> 24) & 0xFF);

    relay_send(RMSG_JOIN_SESSION, payload, 4);
    {
        tU32 deadline = PDGetTotalTime() + RELAY_JOIN_TIMEOUT_MS;
        for (;;) {
            tU32 now = PDGetTotalTime();
            int remaining = (int)(deadline - now);
            if (remaining <= 0) {
                len = -1;
                break;
            }
            len = relay_recv_wait(&msg_type, resp, remaining);
            if (len < 0) {
                break; // timeout
            }
            if (msg_type == RMSG_SESSION_JOINED || msg_type == RMSG_SESSION_ERROR) {
                break; // got a definitive answer
            }
            // Ignore unrelated packets (e.g. INCOMING_PACKET resends from host)
        }
    }
    if (len < 1 || msg_type != RMSG_SESSION_JOINED) {
        dr_dprintf("Relay_JoinGame: relay rejected join (session %u)", (unsigned)session_id);
        return 1;
    }

    gRelay_session_id = session_id;
    gRelay_slot = resp[0];
    dr_dprintf("Relay_JoinGame: joined session %u as slot %d", (unsigned)session_id, (int)gRelay_slot);
    return 0;
}

// Join the relay session early so game-level messages can flow before NetJoinGame.
// Safe to call before ChooseNetCar; Relay_JoinGame will re-join and get the same slot.
// Returns 0 on success, non-zero on failure.
int Relay_PreJoinForCarSelection(tNet_game_details* pDetails) {
    if (!Relay_IsEnabled()) {
        return 0;
    }
    if (gRelay_session_id != 0 && gRelay_slot != 0xFF) {
        return 0; // already in a session
    }
    return Relay_JoinGame(pDetails, "");
}

void Relay_LeaveGame(void) {
    uint8_t payload[4];
    if (gRelay_session_id == 0 || gRelay_slot == 0xFF) {
        return;
    }
    payload[0] = (uint8_t)(gRelay_session_id & 0xFF);
    payload[1] = (uint8_t)((gRelay_session_id >> 8) & 0xFF);
    payload[2] = (uint8_t)((gRelay_session_id >> 16) & 0xFF);
    payload[3] = (uint8_t)((gRelay_session_id >> 24) & 0xFF);
    relay_send(RMSG_LEAVE_SESSION, payload, 4);
    gRelay_session_id = 0;
    gRelay_slot = 0xFF;
}

// Refresh the relay server's last_seen timestamp so that a prolonged DisableNetService
// period (e.g. LoadCar blocking the main loop) does not silently expire the session.
void Relay_SendHeartbeat(void) {
    uint8_t payload[4];
    if (gRelay_session_id == 0 || gRelay_slot == 0xFF) {
        return;
    }
    payload[0] = (uint8_t)(gRelay_session_id & 0xFF);
    payload[1] = (uint8_t)((gRelay_session_id >> 8) & 0xFF);
    payload[2] = (uint8_t)((gRelay_session_id >> 16) & 0xFF);
    payload[3] = (uint8_t)((gRelay_session_id >> 24) & 0xFF);
    relay_send(RMSG_HEARTBEAT, payload, 4);
}

tNet_message* Relay_GetNextMessage(tNet_game_details* pDetails, void** ppSender_address) {
    uint8_t payload[RELAY_MAX_PAYLOAD];
    uint8_t msg_type;
    int len;

    for (;;) {
        len = relay_recv_nowait(&msg_type, payload);
        if (len < 0) {
            return NULL;
        }
        if (msg_type == RMSG_PLAYER_JOINED || msg_type == RMSG_PLAYER_LEFT) {
            // Game handles join/leave through the SENDMEDETAILS/JOIN message exchange.
            continue;
        }
        if (msg_type != RMSG_INCOMING_PACKET) {
            continue;
        }
        break;
    }

    // INCOMING_PACKET: session_id(4) + from_slot(1) + data[]
    if (len < 5) {
        return NULL;
    }
    uint32_t from_session = (uint32_t)payload[0]
        | ((uint32_t)payload[1] << 8)
        | ((uint32_t)payload[2] << 16)
        | ((uint32_t)payload[3] << 24);
    uint8_t from_slot = payload[4];

    // Cap incoming game data to the broadcast stack maximum so tMax_message never overflows.
    // The original cap used sizeof(tNet_message)=380, but broadcast stacks can be up to
    // MAX_MESAGE_STACK_SIZE=512 bytes.  Truncating to 380 bytes left trailing contents entries
    // pointing into uninitialised memory, which could be misread as NETMSGID_HOSTICIDE (0x08)
    // and cause spurious client disconnects.
    uint16_t data_len = (uint16_t)(len - 5);
    if (data_len > (uint16_t)RELAY_GAME_MAX_STACK) {
        data_len = (uint16_t)RELAY_GAME_MAX_STACK;
    }

    tNet_message* msg = NetAllocateMessage(data_len);
    if (msg == NULL) {
        return NULL;
    }
    memcpy(msg, payload + 5, data_len);
    // Force overall_size to match what was actually received.
    // Without this, an attacker controls this field and any downstream code that
    // does memcpy(dst, msg, msg->overall_size) would overflow.
    msg->overall_size = data_len;

    gRelay_sender_addr = relay_addr_make(from_session, from_slot);
    *ppSender_address = &gRelay_sender_addr;
    return msg;
}

int Relay_SendMessageToAllPlayers(tNet_game_details* pDetails, tNet_message* pMessage) {
    uint16_t msg_size = pMessage->overall_size;
    if (msg_size > RELAY_GAME_MAX_STACK) {
        msg_size = RELAY_GAME_MAX_STACK;
    }
    // GAME_PACKET: session_id(4) + dest_slot(1) + data[]
    uint8_t payload[5 + RELAY_GAME_MAX_STACK];

    payload[0] = (uint8_t)(gRelay_session_id & 0xFF);
    payload[1] = (uint8_t)((gRelay_session_id >> 8) & 0xFF);
    payload[2] = (uint8_t)((gRelay_session_id >> 16) & 0xFF);
    payload[3] = (uint8_t)((gRelay_session_id >> 24) & 0xFF);
    payload[4] = 0xFF; // broadcast to all
    memcpy(payload + 5, pMessage, msg_size);

    relay_send(RMSG_GAME_PACKET, payload, (uint16_t)(5 + msg_size));
    NetDisposeMessage(pDetails, pMessage);
    return 0;
}

int Relay_SendMessageToAddress(tNet_game_details* pDetails, tNet_message* pMessage, void* pAddress) {
    tCopyable_sockaddr_in* addr = (tCopyable_sockaddr_in*)pAddress;
    uint8_t dest_slot;
    uint32_t session_id;

    if (relay_addr_is_relay(addr)) {
        session_id = relay_addr_session(addr);
        dest_slot = relay_addr_slot(addr);
    } else {
        // Shouldn't happen in relay mode, fall back to broadcast.
        session_id = gRelay_session_id;
        dest_slot = 0xFF;
    }

    uint16_t msg_size = pMessage->overall_size;
    if (msg_size > RELAY_GAME_MAX_STACK) {
        msg_size = RELAY_GAME_MAX_STACK;
    }
    // GAME_PACKET: session_id(4) + dest_slot(1) + data[]
    uint8_t payload[5 + RELAY_GAME_MAX_STACK];

    payload[0] = (uint8_t)(session_id & 0xFF);
    payload[1] = (uint8_t)((session_id >> 8) & 0xFF);
    payload[2] = (uint8_t)((session_id >> 16) & 0xFF);
    payload[3] = (uint8_t)((session_id >> 24) & 0xFF);
    payload[4] = dest_slot;
    memcpy(payload + 5, pMessage, msg_size);

    relay_send(RMSG_GAME_PACKET, payload, (uint16_t)(5 + msg_size));
    NetDisposeMessage(pDetails, pMessage);
    return 0;
}
