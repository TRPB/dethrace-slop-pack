// Added by dethrace: scaled panel + animated nameplate for player opponents
// (Max Damage / Die Anna).  All new code lives here; base files only call in.

#include "player_opponent.h"

#if defined(DETHRACE_FIX_BUGS)

#include "brender.h"
#include "constants.h"
#include "dr_types.h"
#include "flicplay.h"
#include "globvars.h"
#include "racestrt.h"
#include "utility.h"

// FLI player state for the animated nameplate that fills the bottom gap of the
// scaled panel (the area below the upscaled character FLI).
static tFlic_descriptor s_nameplate_flic[2];
static tU8*             s_nameplate_data[2];
static tU32             s_nameplate_data_len[2];
static br_pixelmap*     s_nameplate_buffer[2];

// Returns the current nameplate pixelmap for slot pIndex, or NULL if inactive.
br_pixelmap* PlayerOpponent_GetNameplatePixelmap(int pIndex) {
    return s_nameplate_buffer[pIndex];
}

// Advance the nameplate FLI by one frame, looping when it reaches the end.
// Called once per main-panel FLI frame from ServicePanelFlics.
void PlayerOpponent_ServiceNameplate(int pIndex) {
    if (!gPanel_scale[pIndex] || s_nameplate_data[pIndex] == NULL) {
        return;
    }
    int done = PlayNextFlicFrame2(&s_nameplate_flic[pIndex], 1);
    if (done) {
        EndFlic(&s_nameplate_flic[pIndex]);
        BrPixelmapFill(s_nameplate_buffer[pIndex], 0);
        StartFlic("", pIndex, &s_nameplate_flic[pIndex],
                  s_nameplate_data_len[pIndex], (tS8*)s_nameplate_data[pIndex],
                  s_nameplate_buffer[pIndex], 0, 0, 0);
    }
}

// Free nameplate resources for slot pIndex and reset scaling.
// Called from ChangePanelFlic and DisposeFlicPanel.
void PlayerOpponent_ResetPanel(int pIndex) {
    EndFlic(&s_nameplate_flic[pIndex]);
    if (s_nameplate_buffer[pIndex] != NULL) {
        BrMemFree(s_nameplate_buffer[pIndex]->pixels);
        BrPixelmapFree(s_nameplate_buffer[pIndex]);
        s_nameplate_buffer[pIndex] = NULL;
    }
    s_nameplate_data[pIndex]     = NULL;
    s_nameplate_data_len[pIndex] = 0;
    gPanel_scale[pIndex]         = 0;
}

// Set up (or clear) the animated nameplate FLI for slot pIndex.
// Pass pData=NULL to clear.
void PlayerOpponent_SetNameplateFlic(int pIndex, tU8* pData, tU32 pLen) {
    EndFlic(&s_nameplate_flic[pIndex]);
    if (s_nameplate_buffer[pIndex] != NULL) {
        BrMemFree(s_nameplate_buffer[pIndex]->pixels);
        BrPixelmapFree(s_nameplate_buffer[pIndex]);
        s_nameplate_buffer[pIndex] = NULL;
    }
    s_nameplate_data[pIndex]     = pData;
    s_nameplate_data_len[pIndex] = pLen;
    if (pData != NULL && pLen >= 12) {
        int np_w = (int)(tU16)(pData[8]  | ((tU16)pData[9]  << 8));
        int np_h = (int)(tU16)(pData[10] | ((tU16)pData[11] << 8));
        void* np_pixels = BrMemAllocate(np_h * ((np_w + 3) & ~3), kFlic_panel_pixels);
        s_nameplate_buffer[pIndex] = DRPixelmapAllocate(gBack_screen->type, np_w, np_h, np_pixels, 0);
        BrPixelmapFill(s_nameplate_buffer[pIndex], 0);
        StartFlic("", pIndex, &s_nameplate_flic[pIndex], pLen, (tS8*)pData,
                  s_nameplate_buffer[pIndex], 0, 0, 0);
    }
}

// Override for SetOpponentFlic: player opponents (car_number 100/101) use the
// original character FLI scaled to fill the panel + a nameplate FLI below it.
// Returns 1 if handled, 0 if the caller should use its normal path.
int PlayerOpponent_SetOpponentFlic(void) {
    static tU8* s_frank_data = NULL;
    static tU32 s_frank_len  = 0;
    static tU8* s_annie_data = NULL;
    static tU32 s_annie_len  = 0;

    int idx     = gCurrent_race.opponent_list[gOpponent_index].index;
    int car_num = gOpponents[idx].car_number;
    if (car_num != 100 && car_num != 101) {
        return 0;
    }
    tU8** pp_data    = (car_num == 100) ? &s_frank_data : &s_annie_data;
    tU32* pp_len     = (car_num == 100) ? &s_frank_len  : &s_annie_len;
    char* name = (char*)((car_num == 100) ? "FRANKVIEW.FLI" : "ANNIEVIEW.FLI");
    LoadFlicData(name, pp_data, pp_len);
    // ChangePanelFlic resets scale/nameplate via PlayerOpponent_ResetPanel.
    ChangePanelFlic(0,
        gOpponents[idx].mug_shot_image_data,
        gOpponents[idx].mug_shot_image_data_length);
    gPanel_scale[0] = 1;
    if (*pp_data != NULL) {
        PlayerOpponent_SetNameplateFlic(0, *pp_data, *pp_len);
    }
    return 1;
}

#endif // DETHRACE_FIX_BUGS
