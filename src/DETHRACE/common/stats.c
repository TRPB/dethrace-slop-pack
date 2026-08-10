#include "stats.h"

#if defined(DETHRACE_FIX_BUGS)

#include "globvars.h"
#include "harness/meld.h"
#include "loading.h"
#include "utility.h"
#include "pd/sys.h"
#include <string.h>
#include <sys/stat.h>

// Added by dethrace — in-memory stats for the active save slot
tGame_stats gGame_stats;

static tU32 StatsCalcChecksum(const tGame_stats* s) {
    const tU8* p = (const tU8*)s;
    tU32 cs = 0;
    int i;
    for (i = 0; i < (int)(sizeof(tGame_stats) - sizeof(tU32)); i++) {
        cs ^= (tU32)p[i] * (tU32)(i + 1);
    }
    return cs;
}

static void StatsGetPath(tPath_name the_path, int slot) {
    PathCat(the_path, gApplication_path, gMeld_active ? "SAVEGAME_M" : "SAVEGAME");
    PathCat(the_path, the_path, "STATSx");
    the_path[strlen(the_path) - 1] = '0' + slot;
}

void Stats_Load(int slot) {
    tPath_name the_path;
    FILE* f;
    tU32 the_size;

    memset(&gGame_stats, 0, sizeof(gGame_stats));
    gGame_stats.version = STATS_VERSION;

    StatsGetPath(the_path, slot);
    f = DRfopen(the_path, "rb");
    if (f == NULL) {
        return;
    }
    the_size = GetFileLength(f);
    if (the_size == sizeof(tGame_stats)) {
        fread(&gGame_stats, 1, sizeof(tGame_stats), f);
        if (gGame_stats.version != STATS_VERSION
                || StatsCalcChecksum(&gGame_stats) != gGame_stats.checksum) {
            memset(&gGame_stats, 0, sizeof(gGame_stats));
            gGame_stats.version = STATS_VERSION;
        }
    }
    fclose(f);
}

void Stats_Save(int slot) {
    tPath_name dir_path;
    tPath_name the_path;
    FILE* f;

    gGame_stats.version = STATS_VERSION;
    gGame_stats.checksum = StatsCalcChecksum(&gGame_stats);

    PathCat(dir_path, gApplication_path, gMeld_active ? "SAVEGAME_M" : "SAVEGAME");
    mkdir(dir_path, 0755);

    StatsGetPath(the_path, slot);
    PDFileUnlock(the_path);
    f = DRfopen(the_path, "wb");
    if (f == NULL) return;
    fwrite(&gGame_stats, 1, sizeof(gGame_stats), f);
    fclose(f);
}

void Stats_OnPedKilled(void) {
    gGame_stats.total_peds_killed++;
    if ((tU32)gProgram_state.peds_killed > gGame_stats.max_peds_single_race) {
        gGame_stats.max_peds_single_race = (tU32)gProgram_state.peds_killed;
    }
}

void Stats_OnPowerupCollected(void) {
    gGame_stats.total_powerups_collected++;
}

void Stats_OnRaceResult(tRace_over_reason reason) {
    if (reason == eRace_over_laps) {
        gGame_stats.total_races_won++;
        gGame_stats.races_won_by_checkpoints++;
    } else if (reason == eRace_over_peds) {
        gGame_stats.total_races_won++;
        gGame_stats.races_won_by_peds++;
    } else if (reason == eRace_over_opponents) {
        gGame_stats.total_races_won++;
        gGame_stats.races_won_by_opponents++;
    }
}

void Stats_OnOpponentWasted(void) {
    gGame_stats.total_opponents_wasted++;
}

void Stats_OnRepairSpent(tU32 amount) {
    gGame_stats.total_repair_spent += amount;
}

void Stats_OnCreditsEarned(tU32 amount) {
    gGame_stats.total_credits_earned += amount;
}

#endif // DETHRACE_FIX_BUGS
