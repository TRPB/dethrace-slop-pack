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
    tU32 _reserved[5];
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

#endif // DETHRACE_FIX_BUGS

#endif // STATS_H
