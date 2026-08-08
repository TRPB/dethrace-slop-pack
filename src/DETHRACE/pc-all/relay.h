#ifndef PC_ALL_RELAY_H
#define PC_ALL_RELAY_H

#include "dr_types.h"
#include "net_types.h"

// UDP relay client for internet multiplayer without port forwarding.
// Enabled by setting RelayServer=ip:port under [Slop] in dethrace.ini.
// Hook points in allnet.c call Relay_IsEnabled() first; if true they delegate
// to functions here instead of the LAN broadcast/direct-UDP path.

int Relay_IsEnabled(void);

// Called from PDNetInitialise after the game UDP socket is ready.
// Returns 0 on success, -1 on failure (relay is silently disabled).
int Relay_Init(void);

void Relay_Shutdown(void);

// Fetch session list from relay and populate joinable/count.
// Replaces the LAN broadcast in PDNetStartProducingJoinList.
void Relay_StartProducingJoinList(tPD_net_game_info* joinable, int* count);

// Return session at pIndex as a skeletal tNet_game_details with relay address.
// Returns 1 if available, 0 if not.
int Relay_GetNextJoinGame(tNet_game_details* pGame, int pIndex);

// Register a hosted session with the relay server.
// Returns 1 on success, 0 on failure.
int Relay_HostGame(tNet_game_details* pDetails, char* pHost_name, void** ppHost_address);

// Join a relay session identified by the fake address in pDetails.
// Returns 0 on success (matching PDNetJoinGame convention).
int Relay_JoinGame(tNet_game_details* pDetails, char* pPlayer_name);

// Join the relay session before ChooseNetCar so game messages can flow.
// No-op if relay is disabled or already in a session.
int Relay_PreJoinForCarSelection(tNet_game_details* pDetails);

// Send LEAVE_SESSION if currently in a session.
void Relay_LeaveGame(void);

// Send HEARTBEAT to refresh the relay server's last_seen timestamp.
// Call before and after a DisableNetService period to prevent session expiry.
void Relay_SendHeartbeat(void);

// Non-blocking receive. Returns an allocated tNet_message or NULL.
tNet_message* Relay_GetNextMessage(tNet_game_details* pDetails, void** ppSender_address);

// Send pMessage to all players in the current relay session.
int Relay_SendMessageToAllPlayers(tNet_game_details* pDetails, tNet_message* pMessage);

// Send pMessage to the player encoded in pAddress (a tCopyable_sockaddr_in*).
int Relay_SendMessageToAddress(tNet_game_details* pDetails, tNet_message* pMessage, void* pAddress);

#endif
