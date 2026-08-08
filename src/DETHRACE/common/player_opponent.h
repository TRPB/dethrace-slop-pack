#ifndef _PLAYER_OPPONENT_H_
#define _PLAYER_OPPONENT_H_

// Added by dethrace: scaled panel + animated nameplate for player opponents.

#if defined(DETHRACE_FIX_BUGS)

#include "brender.h"
#include "dr_types.h"

br_pixelmap* PlayerOpponent_GetNameplatePixelmap(int pIndex);
void PlayerOpponent_ServiceNameplate(int pIndex);
void PlayerOpponent_ResetPanel(int pIndex);
void PlayerOpponent_SetNameplateFlic(int pIndex, tU8* pData, tU32 pLen);
int  PlayerOpponent_SetOpponentFlic(void);

#endif // DETHRACE_FIX_BUGS

#endif
