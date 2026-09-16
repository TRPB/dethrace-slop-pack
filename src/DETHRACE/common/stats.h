#ifndef STATS_H
#define STATS_H

#if defined(DETHRACE_FIX_BUGS)

#include "dr_types.h"

// Added by dethrace — cumulative per-save-slot statistics
#define STATS_VERSION 1

typedef struct tGame_stats {
    tU32 version;
    tU32 total_peds_killed;
    tU32 total_powerups_collected;
    tU32 total_races_won;
    tU32 races_won_by_peds;
    tU32 races_won_by_checkpoints;
    tU32 races_won_by_opponents;
    tU32 total_opponents_wasted;
    tU32 max_peds_single_race;
    tU32 total_repair_spent;
    tU32 total_credits_earned;
    // High-water marks for the single-race achievements. Carved out of
    // _reserved, so the struct keeps its size and old saves still load and
    // checksum — the reserved words were already zero, which is the right
    // starting value for a maximum.
    tU32 max_cows_single_race;
    tU32 max_credits_single_race;
    tU32 _reserved[3];
    tU32 checksum; // must be last
} tGame_stats;

extern tGame_stats gGame_stats;

void Stats_Load(int slot);
void Stats_Save(int slot);
void Stats_OnPedKilled(void);
void Stats_OnPowerupCollected(void);
void Stats_OnRaceResult(tRace_over_reason reason);
void Stats_OnOpponentWasted(void);
void Stats_OnRepairSpent(tU32 amount);
void Stats_OnCreditsEarned(tU32 amount);
void Stats_OnCowKilled(tU32 race_cow_kills);
void Stats_OnRaceCreditsEarned(tU32 race_credits);

#endif // DETHRACE_FIX_BUGS

#endif // STATS_H
