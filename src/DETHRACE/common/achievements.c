#include "achievements.h"
#include "pedestrn.h"
#include "stats.h"

#include <string.h>
#include <sys/stat.h>
#include "brender.h"
#include "harness/meld.h"
#include "harness/trace.h"
#include "constants.h"
#include "displays.h"
#include "flicplay.h"
#include "globvars.h"
#include "globvrbm.h"
#include "grafdata.h"
#include "graphics.h"
#include "harness/config.h"
#include "intrface.h"
#include "loading.h"
#include "pd/sys.h"
#include "racestrt.h"
#include "sound.h"
#include "utility.h"

#if defined(DETHRACE_FIX_BUGS)

// Added by dethrace
// image_file for PIX: "CONTAINER.PIX|FRAMENAME.PIX" to load a specific still frame.
// image_file for FLI: just the filename; freeze on first frame via ChangePanelFlic+EndFlic.
static const tAchievement gAchievement_list[] = {
    { "Racer", "Win a race", 0, 0, eAchTrigger_race_won, NULL, "RACEOVER.PIX|RACEOVER.PIX" },
    { "Winner Winner Chicken Killer", "Win 10 races", 0, 0, eAchTrigger_race_won, NULL, "TRAFLIT3.PIX|traflit3.pix" },
    { "Cop Killer", "Destroy a police car", 0, 0, eAchTrigger_none, NULL, "COP.PIX|COPTRN.PIX" },
    { "Hit n Run", "Run over a pedestrian", 0, 0, eAchTrigger_ped_killed, NULL, "ANN.PIX|ANTRN.PIX" },
    { "Massacre", "Run over 100 pedestrians", 0, 0, eAchTrigger_ped_killed, NULL, "OLD.PIX|oldtrn.pix" },
    { "I'm Coming to Get You", "Run over 1,000 pedestrians", 0, 0, eAchTrigger_ped_killed, NULL, "GOD.PIX|godtrn.pix" },
    { "Pedicide", "Win a race by killing all the pedestrians", 0, 0, eAchTrigger_none, NULL, "TIA.PIX|tiatrn.pix" },
    { "I Was in the War", "Win a race by destroying all opponents", 0, 0, eAchTrigger_none, NULL, "ANNIE.FLI" },
    { "Most Wanted", "Win every race in the game", 0, 0, eAchTrigger_race_won, NULL, "FRANK.FLI" },
    { "Pinball Wizard", "Kill 5 pedestrians with the same object during pinball mode", 0, 0, eAchTrigger_ped_killed, NULL, "MAN.PIX|MANRUNA1.PIX" },
    { "Pacifist", "Win a race without killing any pedestrians yourself", 0, 0, eAchTrigger_race_won, NULL, "GRN.PIX|grntrn.pix" },
    { "Racist", "Win a race the boring way by collecting all the checkpoints", 0, 0, eAchTrigger_race_won, NULL, "CHECKPNT.PIX|CHECKPNT.PIX" },
    { "Patient Zero", "Be the first to run over a pedestrian in a race", 0, 0, eAchTrigger_ped_killed, NULL, "BUS.PIX|BUSRUNA1.PIX" },
    { "Speed Bump", "Run over a pedestrian at under 5mph", 0, 0, eAchTrigger_ped_killed, NULL, "FAT.PIX|fattrn.pix" },
    { "Road Pizza", "Kill a pedestrian at over 150mph", 0, 0, eAchTrigger_ped_killed, NULL, "BIK.PIX|BIKRUNA1.PIX" },
    { "Lawn Mower", "Kill 20 pedestrians in a row without stopping", 0, 0, eAchTrigger_ped_killed, NULL, "BAT.PIX|battrn.pix" },
    { "Turkey", "Kill 3 pedestrians simultaneously", 0, 0, eAchTrigger_ped_killed, NULL, "CPT.PIX|CPTTRN.PIX" },
    { "Rampage", "Kill 10 pedestrians within 10 seconds", 0, 0, eAchTrigger_ped_killed, NULL, "PER.PIX|PERTRN.PIX" },
    { "Going Postal", "Kill 100 pedestrians in a single race", 0, 0, eAchTrigger_ped_killed, NULL, "CRM.PIX|crmtrn.pix" },
    { "Hunter", "Kill 500 pedestrians in a single race", 0, 0, eAchTrigger_ped_killed, NULL, "RID.PIX|RIDSHK2.PIX" },
    { "Public Health Crisis", "Kill 5,000 pedestrians total", 0, 0, eAchTrigger_ped_killed, NULL, "COP.PIX|COPTRN.PIX" },
    { "Extinction Event", "Kill 10,000 pedestrians total", 0, 0, eAchTrigger_ped_killed, NULL, "KEL.PIX|keltrn.pix" },
    { "Environmental Hazard", "Kill a pedestrian using a physics object, not your car", 0, 0, eAchTrigger_ped_killed, NULL, "LTH.PIX|lthtrn.pix" },
    { "Bovine Intervention", "Run over a cow", 0, 0, eAchTrigger_ped_killed, NULL, "MOO.PIX|MOOSHK3.PIX" },
    { "Whole Herd", "Run over 30 cows in a single race", 0, 0, eAchTrigger_ped_killed, NULL, "MOO.PIX|MOOSHK3.PIX" },
    { "Grim Reaper", "Win 10 races by killing all pedestrians", 0, 0, eAchTrigger_none, NULL, "SKN.PIX|SKNHIT4B.PIX" },
    { "Not much of a sports fan", "Kill every football player in the stadium", 0, 0, eAchTrigger_ped_killed, NULL, "COW.PIX|COWHIT4B.PIX" },
    { "First Blood", "Wreck your first opponent", 0, 0, eAchTrigger_opponent_wasted, NULL, "UWASTED.PIX|uwasted.pix" },
    { "Demolition Derby", "Wreck 2 opponents within 30 seconds of each other", 0, 0, eAchTrigger_opponent_wasted, NULL, "UWASTED.PIX|uwasted.pix" },
    { "Road Rage", "Wreck 3 opponents in a single race", 0, 0, eAchTrigger_opponent_wasted, NULL, "UWASTED.PIX|uwasted.pix" },
    { "Total Wipeout", "Wreck all opponents in a race", 0, 0, eAchTrigger_opponent_wasted, NULL, "UWASTED.PIX|uwasted.pix" },
    { "Scrap Dealer", "Wreck 100 opponents total", 0, 0, eAchTrigger_opponent_wasted, NULL, "UWASTED.PIX|uwasted.pix" },
    { "Serial Killer", "Win 10 races by destroying all opponents", 0, 0, eAchTrigger_none, NULL, "UWASTED.PIX|uwasted.pix" },
    { "Null and void", "Leave the bounds of map", 0, 0, eAchTrigger_none, NULL, "FRANK.FLI" },
    { "My Car Now", "Win a race in a stolen opponent's car", 0, 0, eAchTrigger_none, NULL, "VLAD.PIX|vdtop.pix" },
    { "Collector", "Acquire every opponent's car", 0, 0, eAchTrigger_none, NULL, "IVAN.PIX|vsus.pix" },
    { "Test Drive", "Complete a race using 10 different cars", 0, 0, eAchTrigger_race_won, NULL, "MERC.PIX|mcwing.pix" },
    { "Frequent Flyer", "Keep your car airborne for 5 seconds", 0, 0, eAchTrigger_none, NULL, "STUNT.PIX|CCROAD.PIX" },
    { "High Flyer", "Launch off a ramp and land without taking damage", 0, 0, eAchTrigger_none, NULL, "ARENA.PIX|CRATEND.PIX" },
    { "Close Shave", "Pass within 1m of a pedestrian at over 100mph without hitting them", 0, 0, eAchTrigger_none, NULL, "CAM.PIX|CAMTRN.PIX" },
    { "Skid Mark", "Perform a continuous skid for more than 100m", 0, 0, eAchTrigger_none, NULL, "GIBSMEAR.PIX|gibsmear.pix" },
    { "Insurance Nightmare", "Accumulate 1,000,000 credits in total repair costs", 0, 0, eAchTrigger_none, NULL, "FRANK.FLI" },
    { "Not Mine", "Kill an opponent by pushing them onto a mine", 0, 0, eAchTrigger_opponent_wasted, NULL, "MINE.PIX|miner5.pix" },
    { "Credit Where It's Due", "Earn 100,000 credits in a single race", 0, 0, eAchTrigger_none, NULL, "FRANK.FLI" },
    { "Millionaire", "Accumulate 1,000,000 credits total", 0, 0, eAchTrigger_none, NULL, "ANNIE.FLI" },
    { "Power Hungry", "Collect 10 different power-ups in a single race", 0, 0, eAchTrigger_powerup, NULL, "FRANK.FLI" },
    { "Zap", "Kill 50 peds with a single electro bastard ray", 0, 0, eAchTrigger_powerup, NULL, "NIK.PIX|NIKTRN.PIX" },
    { "Juiced", "Kill Agent Orange", 0, 0, eAchTrigger_opponent_wasted, "Agent Orange", NULL },
    { "Alfonso-long and farwell", "Waste Alfonso Spaghetti", 0, 0, eAchTrigger_opponent_wasted, "Alfonso Spaghetti", NULL },
    { "Ashes to Ashes", "Waste The Ashteroid", 0, 0, eAchTrigger_opponent_wasted, "The Ashteroid", NULL },
    { "Auto Succumb", "Waste Auto Scum", 0, 0, eAchTrigger_opponent_wasted, "Auto Scum", NULL },
    { "Big Deaddy", "Waste Big Daddy", 0, 0, eAchTrigger_opponent_wasted, "Big Daddy", NULL },
    { "Brotherly Hate", "Waste the Brothers Grimm", 0, 0, eAchTrigger_opponent_wasted, "The Brothers Grimm", NULL },
    { "Shirley Not", "Waste Burley Shirley", 0, 0, eAchTrigger_opponent_wasted, "Burly Shirley", NULL },
    { "Ignition failure", "Waste Carkey & Clutch", 0, 0, eAchTrigger_opponent_wasted, "Carkey & Clutch", NULL },
    { "Don in", "Waste Don Dumpster", 0, 0, eAchTrigger_opponent_wasted, "Don Dumpster", NULL },
    { "Ed 101 Ways to Die", "Waste Ed 101", 0, 0, eAchTrigger_opponent_wasted, "Ed 101", NULL },
    { "Dead Hunter", "Waste Ed Hunter", 0, 0, eAchTrigger_opponent_wasted, "Ed Hunter", NULL },
    { "Burn ward", "Waste Firestorm", 0, 0, eAchTrigger_opponent_wasted, "Firestorm", NULL },
    { "Half dead harry", "Waste Halfwit Harry", 0, 0, eAchTrigger_opponent_wasted, "Halfwit Harry", NULL },
    { "Hammerdead", "Waste Hammerhead", 0, 0, eAchTrigger_opponent_wasted, "Hammerhead", NULL },
    { "Heinz Been", "Waste Heinz Faust", 0, 0, eAchTrigger_opponent_wasted, "Heinz Faust", NULL },
    { "Shweinter of discontent", "Waste Helga Shwein", 0, 0, eAchTrigger_opponent_wasted, "Helga Shwein", NULL },
    { "Do the monster mash", "Waste Herman Monster", 0, 0, eAchTrigger_opponent_wasted, "Herman Monster", NULL },
    { "From Crusher With Love", "Waste Ivan the Bastard", 0, 0, eAchTrigger_opponent_wasted, "Ivan the Bastard", NULL },
    { "Kut down", "Waste Kutter", 0, 0, eAchTrigger_opponent_wasted, "Kutter", NULL },
    { "Laydie bug", "Waste Lady bug", 0, 0, eAchTrigger_opponent_wasted, "Lady Bug", NULL },
    { "Scar Scarlett", "Waste Madam Scarlett", 0, 0, eAchTrigger_opponent_wasted, "Madam Scarlett", NULL },
    { "Mech ado about nothing", "Waste Mech Maniac", 0, 0, eAchTrigger_opponent_wasted, "Mech Maniac", NULL },
    { "You don't need roads", "Waste Metal Mikey", 0, 0, eAchTrigger_opponent_wasted, "Metal Mikey", NULL },
    { "Blow the bloody doors off", "Waste Mike O'Kane", 0, 0, eAchTrigger_opponent_wasted, "Mike O'Kane", NULL },
    { "To the Moon", "Waste Moon Child", 0, 0, eAchTrigger_opponent_wasted, "Moon Child", NULL },
    { "Truck Stop", "Waste Mother Trucker", 0, 0, eAchTrigger_opponent_wasted, "Mother Trucker", NULL },
    { "I'm Not Okay", "Waste OK Stimpson", 0, 0, eAchTrigger_opponent_wasted, "OK Stimpson", NULL },
    { "Otis Deading", "Waste Otis P Jivefunk", 0, 0, eAchTrigger_opponent_wasted, "Otis P Jivefunk", NULL },
    { "Psycho Killer, qu'est-ce que c'est?", "Waste Psycho Pitbull", 0, 0, eAchTrigger_opponent_wasted, "Psycho Pitbull", NULL },
    { "Curiosity Killed the Cat", "Waste Pussy LaGore", 0, 0, eAchTrigger_opponent_wasted, "Pussy LaGore", NULL },
    { "Came, Saw, and Conquered", "Waste Sawbones", 0, 0, eAchTrigger_opponent_wasted, "Sawbones", NULL },
    { "Screwed Lewie", "Waste Screwie Lewie", 0, 0, eAchTrigger_opponent_wasted, "Screwie Lewie", NULL },
    { "Burst sin to flames", "Waste Sinthea", 0, 0, eAchTrigger_opponent_wasted, "Sinthea", NULL },
    { "Stella Goner", "Waste Stella Stunna", 0, 0, eAchTrigger_opponent_wasted, "Stella Stunna", NULL },
    { "Meteor Strike", "Waste Stig O'Sore", 0, 0, eAchTrigger_opponent_wasted, "Stig O'Sore", NULL },
    { "Borg to Death", "Waste Su Borg", 0, 0, eAchTrigger_opponent_wasted, "Su Borg", NULL },
    { "She's hella Dead", "Waste Val Hella", 0, 0, eAchTrigger_opponent_wasted, "Val Hella", NULL },
    { "Viking Burial", "Waste Vlad", 0, 0, eAchTrigger_opponent_wasted, "Vlad", NULL },
    { "Wanda Lost", "Waste Wanda Lust", 0, 0, eAchTrigger_opponent_wasted, "Wanda Lust", NULL },
};

#define ACHIEVEMENT_COUNT ((int)(sizeof(gAchievement_list) / sizeof(gAchievement_list[0])))
#define ACHIEVEMENT_SAVE_VERSION 2
// Fixed-size save slot so adding achievements doesn't break old saves.
#define ACHIEVEMENT_SLOTS 128
#define ACHIEVEMENT_CAR_SLOTS 40
#define ACHIEVEMENT_CAR_NAME_LEN 16

typedef struct {
    tU32 version;
    tU8 unlocked[ACHIEVEMENT_SLOTS];
    char cars_raced[ACHIEVEMENT_CAR_SLOTS][ACHIEVEMENT_CAR_NAME_LEN];
    tU32 checksum; // must be last
} tAchievement_save;

static tAchievement_save gAch_save;

// Added by dethrace
int gCurrent_achievement_index;

// Added by dethrace — tracks which opponent's mug shot is loaded in panel slot 0 (-1 = none)
static int gAch_mugshot_opponent = -1;

// Added by dethrace — ped image state (PIX or FLI for eAchTrigger_ped_killed achievements)
static br_pixelmap* gAch_ped_pixelmap = NULL;
static tU8* gAch_ped_flic_data = NULL;
static tU32 gAch_ped_flic_len = 0;
static int gAch_ped_flic_w = 0;
static int gAch_ped_flic_h = 0;

// Remap table: gRender_palette index → nearest-colour index in gFlic_palette.
// Pedestrian PIX sprites use render-palette indices; the menu displays via flic palette.
// Built once per achievements screen entry in AchBuildRend2FlcRemap().
static tU8 gAch_rend2flc[256];

// 5-bit-per-channel RGB → nearest flic palette index lookup (32×32×32 = 32 768 entries).
// Built once per achievements screen entry in AchBuildRgbToFlicTable().
static tU8 gAch_rgb5_to_flic[32 * 32 * 32];

// Added by dethrace — Bayer ordered dither for dissolve transition
static const tU8 kBayer4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 },
};
#define kDissolve_ms 200
static tU8 gAch_new_buf[512 * 256];
static tU8 gAch_old_buf[512 * 256];
static int gAch_panel_x;
static int gAch_panel_y;
static int gAch_panel_w;
static int gAch_panel_h;
static int gAch_dissolve_start = -1;
static tU32 gAch_race_opponents_wasted;
static tU32 gAch_race_player_peds_killed;
static int gAch_race_cow_kills;

// Lawn Mower: consecutive player kill streak (resets after 10s gap)
#define kLawnMower_reset_ms 10000
static int gAch_consecutive_player_kills;
static tU32 gAch_last_player_kill_time;

// Turkey / Rampage: rolling kill timestamp window
#define kKillWindow_max 20
static tU32 gAch_kill_times[kKillWindow_max];
static int gAch_kill_count;
static int gAch_kill_head;

// Pinball Wizard: per-physics-object kill count during pinball powerup
#define kPinball_track_max 8
typedef struct { void* actor; int count; } tAch_pinball_entry;
static tAch_pinball_entry gAch_pinball_objects[kPinball_track_max];
static int gAch_pinball_obj_count;

// Patient Zero: player must be first killer in the race
static int gAch_patient_zero_blocked; // set when an opponent kills a ped before the player

// Not much of a sports fan: footballer peds present in this race
static int gAch_footballer_total;
static int gAch_footballer_kills;

// Zap: peds killed by electro bastard ray in one powerup window
static int gAch_proxray_kills;

// Demolition Derby: timestamp of previous opponent kill
static tU32 gAch_last_opponent_kill_time;

// Total Wipeout: how many opponents were present at race start
static int gAch_race_start_opponents;

// Credit Where It's Due: total credits at race start (to compute per-race earned)
static tU32 gAch_credits_race_start;

// Power Hungry: unique powerup indices collected this race
#define kPowerup_seen_max_idx 128
static tU8 gAch_powerup_seen[kPowerup_seen_max_idx];
static int gAch_race_unique_powerups;

// Not Mine: opponent index last mine-hit by player proximity, and when
#define kMine_flag_ms 30000
static int gAch_mine_flagged_idx;
static tU32 gAch_mine_flag_time;

// Frequent Flyer: continuous airborne time
static tU32 gAch_airborne_ms;

// High Flyer: liftoff damage snapshot and airborne start time
#define kHighFlyer_min_air_ms 500
static int gAch_prev_wheels_on_ground;
static int gAch_confirmed_on_ground; // must touch ground before liftoff tracking starts
static int gAch_liftoff_damage;
static tU32 gAch_liftoff_ms;

// Skid Mark: continuous skid distance in metres
static float gAch_skid_distance_m;

// Added by dethrace — in-race achievement popup stack
#define kPopup_in_ms 500
#define kPopup_show_ms 8000
#define kPopup_save_max_w 160
#define kPopup_save_max_h 120
#define kPopup_max_stack 5
#define kPopup_dismiss_interval_ms 500

typedef struct {
    int ach_idx;
    br_pixelmap* pixelmap;
    br_pixelmap* mug_pm;
    int mug_sx, mug_sy, mug_sw, mug_sh;
    tU32 add_time;
    tU32 dismiss_time; // time when dissolve-out starts; 0 = not scheduled
} tAch_popup_slot;

static tAch_popup_slot gAch_popup_slots[kPopup_max_stack];
static int gAch_popup_count = 0;
static tU32 gAch_popup_last_add_time = 0;
static int gAch_popup_dismissing = 0;
static tU8 gAch_popup_save_buf[kPopup_save_max_w * kPopup_save_max_h * 2];
// 5-bit-per-channel RGB → nearest render-palette index (built lazily on first popup trigger).
static tU8 gAch_rgb5_to_rend[32 * 32 * 32];
static int gAch_rend_table_built = 0;

static void AchSavePanel(tU8* buf) {
    int y;
    for (y = 0; y < gAch_panel_h; y++) {
        memcpy(buf + y * gAch_panel_w,
            (tU8*)gBack_screen->pixels + (gAch_panel_y + y) * gBack_screen->row_bytes + gAch_panel_x,
            gAch_panel_w);
    }
}

static void AchStartDissolve(void) {
    memcpy(gAch_old_buf, gAch_new_buf, gAch_panel_w * gAch_panel_h);
    gAch_dissolve_start = PDGetTotalTime();
}

static void AchApplyDissolve(void) {
    int elapsed = PDGetTotalTime() - gAch_dissolve_start;
    int progress, x, y;
    tU8* dst;
    const tU8* src;
    if (elapsed >= kDissolve_ms) {
        gAch_dissolve_start = -1;
        return;
    }
    progress = elapsed * 16 / kDissolve_ms;
    for (y = 0; y < gAch_panel_h; y++) {
        dst = (tU8*)gBack_screen->pixels + (gAch_panel_y + y) * gBack_screen->row_bytes + gAch_panel_x;
        src = gAch_old_buf + y * gAch_panel_w;
        for (x = 0; x < gAch_panel_w; x++) {
            if (kBayer4[y & 3][x & 3] >= (tU8)progress) {
                dst[x] = src[x];
            }
        }
    }
}

// Build nearest-colour remap from gRender_palette indices to gFlic_palette indices.
static void AchBuildRend2FlcRemap(void) {
    const tU8* rp = (const tU8*)gRender_palette->pixels;
    const tU8* fp = (const tU8*)gFlic_palette->pixels;
    int i, j;
    gAch_rend2flc[0] = 0;
    for (i = 1; i < 256; i++) {
        int ri = rp[i * 4], gi = rp[i * 4 + 1], bi = rp[i * 4 + 2];
        int best = 1, best_dist = 0x7fffffff;
        for (j = 1; j < 256; j++) {
            int dr = ri - fp[j * 4], dg = gi - fp[j * 4 + 1], db = bi - fp[j * 4 + 2];
            int dist = dr * dr + dg * dg + db * db;
            if (dist < best_dist) {
                best_dist = dist;
                best = j;
                if (dist == 0) break;
            }
        }
        gAch_rend2flc[i] = (tU8)best;
    }
}

// Build 5-bit-per-channel RGB → flic palette index table for bilinear quantisation.
static void AchBuildRgbToFlicTable(void) {
    const tU8* fp = (const tU8*)gFlic_palette->pixels;
    int r, g, b, i;
    for (r = 0; r < 32; r++) {
        for (g = 0; g < 32; g++) {
            for (b = 0; b < 32; b++) {
                int best = 1, best_dist = 0x7fffffff;
                for (i = 1; i < 256; i++) {
                    int dr = (r << 3) - fp[i * 4];
                    int dg = (g << 3) - fp[i * 4 + 1];
                    int db = (b << 3) - fp[i * 4 + 2];
                    int dist = dr * dr + dg * dg + db * db;
                    if (dist < best_dist) {
                        best_dist = dist;
                        best = i;
                        if (dist == 0) break;
                    }
                }
                gAch_rgb5_to_flic[r * 1024 + g * 32 + b] = (tU8)best;
            }
        }
    }
}

// Build 5-bit-per-channel RGB → nearest render-palette index table for in-race popup bilinear quantisation.
static void AchBuildRgbToRendTable(void) {
    const tU8* rp = (const tU8*)gRender_palette->pixels;
    int r, g, b, i;
    for (r = 0; r < 32; r++) {
        for (g = 0; g < 32; g++) {
            for (b = 0; b < 32; b++) {
                int best = 1, best_dist = 0x7fffffff;
                for (i = 1; i < 256; i++) {
                    int dr = (r << 3) - rp[i * 4];
                    int dg = (g << 3) - rp[i * 4 + 1];
                    int db = (b << 3) - rp[i * 4 + 2];
                    int dist = dr * dr + dg * dg + db * db;
                    if (dist < best_dist) {
                        best_dist = dist;
                        best = i;
                        if (dist == 0) break;
                    }
                }
                gAch_rgb5_to_rend[r * 1024 + g * 32 + b] = (tU8)best;
            }
        }
    }
    gAch_rend_table_built = 1;
}

// Bilinear-filtered scaled blit from src to dst (8-bit paletted, flic palette space).
// remap != NULL: render-palette → flic remap applied first; index 0 = transparent (skipped).
// remap == NULL: source already in flic palette space; all pixels written.
// Transparent neighbours are spread to the nearest opaque pixel before blending
// to avoid dark halos at sprite edges.
static void AchBlitScaled(br_pixelmap* src, int sx, int sy, int sw, int sh,
    br_pixelmap* dst, int dx, int dy, int dw, int dh, const tU8* remap) {
    const tU8* fp = (const tU8*)gFlic_palette->pixels;
    tU8* src_pixels = (tU8*)src->pixels;
    tU8* dst_pixels = (tU8*)dst->pixels;
    int x, y;

    for (y = 0; y < dh; y++) {
        int fy = (dh > 1) ? (y * (sh - 1) * 256 / (dh - 1)) : 0;
        int sy0 = fy >> 8;
        int sy1 = (sy0 + 1 < sh) ? sy0 + 1 : sy0;
        int wy = fy & 0xff;
        tU8* row0 = src_pixels + (sy + sy0) * src->row_bytes;
        tU8* row1 = src_pixels + (sy + sy1) * src->row_bytes;
        tU8* dst_row = dst_pixels + (dy + y) * dst->row_bytes;

        for (x = 0; x < dw; x++) {
            int fx = (dw > 1) ? (x * (sw - 1) * 256 / (dw - 1)) : 0;
            int sx0 = fx >> 8;
            int sx1 = (sx0 + 1 < sw) ? sx0 + 1 : sx0;
            int wx = fx & 0xff;
            int wx1 = 256 - wx, wy1 = 256 - wy;
            int r, g, b;
            tU8 p00 = row0[sx + sx0], p10 = row0[sx + sx1];
            tU8 p01 = row1[sx + sx0], p11 = row1[sx + sx1];

            if (remap != NULL) {
                // nearest-neighbour pixel for transparency test
                tU8 pnn = (wx < 128) ? ((wy < 128) ? p00 : p01) : ((wy < 128) ? p10 : p11);
                if (pnn == 0) continue;
                // spread nearest to transparent neighbours to avoid dark edge halos
                if (!p00) p00 = pnn;
                if (!p10) p10 = pnn;
                if (!p01) p01 = pnn;
                if (!p11) p11 = pnn;
                p00 = remap[p00]; p10 = remap[p10];
                p01 = remap[p01]; p11 = remap[p11];
            }

            // bilinear blend in flic-palette RGB space
            r = (fp[p00 * 4    ] * wx1 * wy1 + fp[p10 * 4    ] * wx * wy1 + fp[p01 * 4    ] * wx1 * wy + fp[p11 * 4    ] * wx * wy) >> 16;
            g = (fp[p00 * 4 + 1] * wx1 * wy1 + fp[p10 * 4 + 1] * wx * wy1 + fp[p01 * 4 + 1] * wx1 * wy + fp[p11 * 4 + 1] * wx * wy) >> 16;
            b = (fp[p00 * 4 + 2] * wx1 * wy1 + fp[p10 * 4 + 2] * wx * wy1 + fp[p01 * 4 + 2] * wx1 * wy + fp[p11 * 4 + 2] * wx * wy) >> 16;

            dst_row[dx + x] = gAch_rgb5_to_flic[(r >> 3) * 1024 + (g >> 3) * 32 + (b >> 3)];
        }
    }
}

// Bilinear-filtered scaled blit for the in-race popup (render-palette space, index 0 = transparent).
// Handles both BR_PMT_INDEX_8 (8-bit paletted) and BR_PMT_RGB_565 (3dfx) back buffers.
static void PopupBlitScaled(br_pixelmap* src, int sx, int sy, int sw, int sh,
    br_pixelmap* dst, int dx, int dy, int dw, int dh) {
    const tU8* rp = (const tU8*)gRender_palette->pixels;
    tU8* src_pixels = (tU8*)src->pixels;
    tU8* dst_pixels = (tU8*)dst->pixels;
    int is_rgb565 = (dst->type == BR_PMT_RGB_565);
    int x, y;
    for (y = 0; y < dh; y++) {
        int fy = (dh > 1) ? (y * (sh - 1) * 256 / (dh - 1)) : 0;
        int sy0 = fy >> 8;
        int sy1 = (sy0 + 1 < sh) ? sy0 + 1 : sy0;
        int wy = fy & 0xff;
        tU8* row0 = src_pixels + (sy + sy0) * src->row_bytes;
        tU8* row1 = src_pixels + (sy + sy1) * src->row_bytes;
        tU8* dst_row = dst_pixels + (dy + y) * dst->row_bytes;
        for (x = 0; x < dw; x++) {
            int fx = (dw > 1) ? (x * (sw - 1) * 256 / (dw - 1)) : 0;
            int sx0 = fx >> 8;
            int sx1 = (sx0 + 1 < sw) ? sx0 + 1 : sx0;
            int wx = fx & 0xff;
            int wx1 = 256 - wx, wy1 = 256 - wy;
            int cr, cg, cb;
            tU8 rend_idx;
            tU8 p00 = row0[sx + sx0], p10 = row0[sx + sx1];
            tU8 p01 = row1[sx + sx0], p11 = row1[sx + sx1];
            tU8 pnn = (wx < 128) ? ((wy < 128) ? p00 : p01) : ((wy < 128) ? p10 : p11);
            if (pnn == 0) continue;
            if (!p00) p00 = pnn;
            if (!p10) p10 = pnn;
            if (!p01) p01 = pnn;
            if (!p11) p11 = pnn;
            cr = (rp[p00 * 4    ] * wx1 * wy1 + rp[p10 * 4    ] * wx * wy1 + rp[p01 * 4    ] * wx1 * wy + rp[p11 * 4    ] * wx * wy) >> 16;
            cg = (rp[p00 * 4 + 1] * wx1 * wy1 + rp[p10 * 4 + 1] * wx * wy1 + rp[p01 * 4 + 1] * wx1 * wy + rp[p11 * 4 + 1] * wx * wy) >> 16;
            cb = (rp[p00 * 4 + 2] * wx1 * wy1 + rp[p10 * 4 + 2] * wx * wy1 + rp[p01 * 4 + 2] * wx1 * wy + rp[p11 * 4 + 2] * wx * wy) >> 16;
            rend_idx = gAch_rgb5_to_rend[(cr >> 3) * 1024 + (cg >> 3) * 32 + (cb >> 3)];
            if (is_rgb565) {
                *(tU16*)(dst_row + (dx + x) * 2) = PaletteEntry16Bit(gRender_palette, rend_idx);
            } else {
                dst_row[dx + x] = rend_idx;
            }
        }
    }
}

// Bilinear-filtered scaled blit for opponent mug shots (flic-palette INDEX_8 source).
// Blends in flic-palette RGB space, then quantises to the nearest render-palette index.
static void PopupBlitScaledMug(br_pixelmap* src, int sx, int sy, int sw, int sh,
    br_pixelmap* dst, int dx, int dy, int dw, int dh) {
    const tU8* fp = (const tU8*)gFlic_palette->pixels;
    tU8* src_pixels = (tU8*)src->pixels;
    tU8* dst_pixels = (tU8*)dst->pixels;
    int is_rgb565 = (dst->type == BR_PMT_RGB_565);
    int x, y;
    for (y = 0; y < dh; y++) {
        int fy = (dh > 1) ? (y * (sh - 1) * 256 / (dh - 1)) : 0;
        int sy0 = fy >> 8;
        int sy1 = (sy0 + 1 < sh) ? sy0 + 1 : sy0;
        int wy = fy & 0xff;
        tU8* row0 = src_pixels + (sy + sy0) * src->row_bytes;
        tU8* row1 = src_pixels + (sy + sy1) * src->row_bytes;
        tU8* dst_row = dst_pixels + (dy + y) * dst->row_bytes;
        for (x = 0; x < dw; x++) {
            int fx = (dw > 1) ? (x * (sw - 1) * 256 / (dw - 1)) : 0;
            int sx0 = fx >> 8;
            int sx1 = (sx0 + 1 < sw) ? sx0 + 1 : sx0;
            int wx = fx & 0xff;
            int wx1 = 256 - wx, wy1 = 256 - wy;
            int cr, cg, cb;
            tU8 rend_idx;
            tU8 p00 = row0[sx + sx0], p10 = row0[sx + sx1];
            tU8 p01 = row1[sx + sx0], p11 = row1[sx + sx1];
            tU8 pnn = (wx < 128) ? ((wy < 128) ? p00 : p01) : ((wy < 128) ? p10 : p11);
            if (pnn == 0) continue;
            if (!p00) p00 = pnn;
            if (!p10) p10 = pnn;
            if (!p01) p01 = pnn;
            if (!p11) p11 = pnn;
            cr = (fp[p00*4]*wx1*wy1 + fp[p10*4]*wx*wy1 + fp[p01*4]*wx1*wy + fp[p11*4]*wx*wy) >> 16;
            cg = (fp[p00*4+1]*wx1*wy1 + fp[p10*4+1]*wx*wy1 + fp[p01*4+1]*wx1*wy + fp[p11*4+1]*wx*wy) >> 16;
            cb = (fp[p00*4+2]*wx1*wy1 + fp[p10*4+2]*wx*wy1 + fp[p01*4+2]*wx1*wy + fp[p11*4+2]*wx*wy) >> 16;
            rend_idx = gAch_rgb5_to_rend[(cr >> 3) * 1024 + (cg >> 3) * 32 + (cb >> 3)];
            if (is_rgb565) {
                *(tU16*)(dst_row + (dx + x) * 2) = PaletteEntry16Bit(gRender_palette, rend_idx);
            } else {
                dst_row[dx + x] = rend_idx;
            }
        }
    }
}

static int AchIsPix(const char* filename) {
    // Check extension of the container (part before | if present, else whole string)
    const char* bar = strchr(filename, '|');
    size_t len = bar ? (size_t)(bar - filename) : strlen(filename);
    return len >= 4 && strncasecmp(filename + len - 4, ".PIX", 4) == 0;
}

// Load a specific named pixelmap from a container file.
// name_in_container == NULL → returns first pixelmap.
// Falls back to first pixelmap if name not found.
static br_pixelmap* AchLoadPixelmap(const char* container, const char* name_in_container) {
    br_pixelmap* maps[64];
    int i, n, found;
    n = (int)LoadPixelmaps((char*)container, maps, COUNT_OF(maps));
    if (n <= 0)
        return NULL;
    found = 0;
    if (name_in_container != NULL) {
        for (i = 0; i < n; i++) {
            if (maps[i] != NULL && maps[i]->identifier != NULL
                    && strcmp(maps[i]->identifier, name_in_container) == 0) {
                found = i;
                break;
            }
        }
    }
    for (i = 0; i < n; i++) {
        if (i != found)
            BrPixelmapFree(maps[i]);
    }
    return maps[found];
}

static void AchGetFlicDimensions(const tU8* data, int* w, int* h) {
    // FLIC header: bytes 8-9 = width, 10-11 = height (little-endian uint16)
    *w = data[8] | (data[9] << 8);
    *h = data[10] | (data[11] << 8);
}

static void AchClearPedImage(void) {
    if (gAch_ped_pixelmap != NULL) {
        BrPixelmapFree(gAch_ped_pixelmap);
        gAch_ped_pixelmap = NULL;
    }
    if (gAch_ped_flic_data != NULL) {
        BrMemFree(gAch_ped_flic_data);
        gAch_ped_flic_data = NULL;
        gAch_ped_flic_len = 0;
    }
    gAch_ped_flic_w = 0;
    gAch_ped_flic_h = 0;
}

// Added by dethrace — load image for current achievement into panel slot 0 or gAch_ped_pixelmap
static void AchLoadPlayerPortrait(void);

static void AchUpdateMugShot(void) {
    const tAchievement* ach;
    int i;
    if (gCurrent_achievement_index < 0) {
        AchLoadPlayerPortrait();
        return;
    }
    ach = &gAchievement_list[gCurrent_achievement_index];

    AchClearPedImage();
    gAch_mugshot_opponent = -1;

    if (ach->trigger == eAchTrigger_opponent_wasted && ach->trigger_param != NULL) {
        for (i = 0; i < gNumber_of_racers; i++) {
            if (strcmp(gOpponents[i].name, ach->trigger_param) == 0) {
                if (gOpponents[i].mug_shot_image_data == NULL) {
                    LoadOpponentMugShot(i);
                }
                if (gOpponents[i].mug_shot_image_data != NULL) {
                    ChangePanelFlic(0, gOpponents[i].mug_shot_image_data, gOpponents[i].mug_shot_image_data_length);
                    EndFlic(&gPanel_flic[0]);
                    gAch_mugshot_opponent = i;
                }
                return;
            }
        }
        return;
    }

    if (ach->image_file != NULL) {
        if (AchIsPix(ach->image_file)) {
            const char* bar = strchr(ach->image_file, '|');
            if (bar != NULL) {
                char container[64];
                int clen = (int)(bar - ach->image_file);
                if (clen < (int)sizeof(container)) {
                    memcpy(container, ach->image_file, clen);
                    container[clen] = '\0';
                    gAch_ped_pixelmap = AchLoadPixelmap(container, bar + 1);
                }
            } else {
                gAch_ped_pixelmap = LoadPixelmap((char*)ach->image_file);
            }
        } else {
            LoadFlicData((char*)ach->image_file, &gAch_ped_flic_data, &gAch_ped_flic_len);
            if (gAch_ped_flic_data != NULL) {
                AchGetFlicDimensions(gAch_ped_flic_data, &gAch_ped_flic_w, &gAch_ped_flic_h);
                ChangePanelFlic(0, gAch_ped_flic_data, gAch_ped_flic_len);
                EndFlic(&gPanel_flic[0]);
            }
        }
        return;
    }

    EndFlic(&gPanel_flic[0]);
    BrPixelmapFill(gPanel_buffer[0], 0);
}

static void AchFreeSlot(tAch_popup_slot* slot) {
    if (slot->pixelmap != NULL) {
        BrPixelmapFree(slot->pixelmap);
        slot->pixelmap = NULL;
    }
    if (slot->mug_pm != NULL) {
        BrMemFree(slot->mug_pm->pixels);
        BrPixelmapFree(slot->mug_pm);
        slot->mug_pm = NULL;
    }
    slot->ach_idx = -1;
}

// Decode the first frame of a FLI into a freshly allocated INDEX_8 pixelmap.
// Returns NULL on failure. Caller must not free flic_data until after the call.
static br_pixelmap* AchDecodeFlic1stFrame(tU8* flic_data, tU32 flic_len, int pm_w, int pm_h) {
    br_pixelmap* pm;
    br_pixelmap* saved;
    void* pix = BrMemAllocate(pm_h * ((pm_w + 3) & ~3), BR_MEMORY_PIXELS);
    if (pix == NULL) return NULL;
    pm = DRPixelmapAllocate(BR_PMT_INDEX_8, pm_w, pm_h, pix, 0);
    if (pm == NULL) { BrMemFree(pix); return NULL; }
    saved = gPanel_buffer[0];
    gPanel_buffer[0] = pm;
    ChangePanelFlic(0, flic_data, flic_len);
    EndFlic(&gPanel_flic[0]);
    gPanel_buffer[0] = saved;
    return pm;
}

// Load image for achievement idx into a popup slot.
static void AchLoadSlotImage(tAch_popup_slot* slot, int idx) {
    const tAchievement* ach = &gAchievement_list[idx];
    int i;
    if (ach->image_file != NULL && AchIsPix(ach->image_file)) {
        const char* bar = strchr(ach->image_file, '|');
        if (bar != NULL) {
            char container[64];
            int clen = (int)(bar - ach->image_file);
            if (clen < (int)sizeof(container)) {
                memcpy(container, ach->image_file, clen);
                container[clen] = '\0';
                slot->pixelmap = AchLoadPixelmap(container, bar + 1);
            }
        } else {
            slot->pixelmap = LoadPixelmap((char*)ach->image_file);
        }
    } else if (ach->image_file != NULL) {
        tU8* flic_data = NULL;
        tU32 flic_len = 0;
        int flic_w = 0, flic_h = 0;
        LoadFlicData((char*)ach->image_file, &flic_data, &flic_len);
        if (flic_data != NULL) {
            AchGetFlicDimensions(flic_data, &flic_w, &flic_h);
            if (flic_w > 0 && flic_h > 0) {
                slot->mug_pm = AchDecodeFlic1stFrame(flic_data, flic_len, flic_w, flic_h);
                if (slot->mug_pm != NULL) {
                    slot->mug_sx = 0;
                    slot->mug_sy = 0;
                    slot->mug_sw = flic_w;
                    slot->mug_sh = flic_h;
                }
            }
            BrMemFree(flic_data);
        }
    } else if (ach->trigger == eAchTrigger_opponent_wasted && ach->trigger_param != NULL) {
        for (i = 0; i < gNumber_of_racers; i++) {
            if (strcmp(gOpponents[i].name, ach->trigger_param) == 0) {
                int panel_w = gCurrent_graf_data->start_race_panel_right - gCurrent_graf_data->start_race_panel_left;
                int panel_h = gCurrent_graf_data->start_race_panel_bottom - gCurrent_graf_data->start_race_panel_top;
                if (gOpponents[i].mug_shot_image_data == NULL) {
                    LoadOpponentMugShot(i);
                }
                if (gOpponents[i].mug_shot_image_data == NULL) break;
                slot->mug_pm = AchDecodeFlic1stFrame(
                    gOpponents[i].mug_shot_image_data,
                    gOpponents[i].mug_shot_image_data_length,
                    panel_w, panel_h);
                if (slot->mug_pm != NULL) {
                    slot->mug_sx = gCurrent_graf_data->dare_mug_left_margin;
                    slot->mug_sy = gCurrent_graf_data->dare_mug_top_margin;
                    slot->mug_sw = gCurrent_graf_data->dare_mugshot_width;
                    slot->mug_sh = gCurrent_graf_data->dare_mugshot_height;
                }
                break;
            }
        }
    }
}

// Added by dethrace — word-wrap helper using the BRender small font
static void DrawWrappedText(int pX, int pY, int pRight, int pBottom, int pColour, br_font* pFont, const char* pText) {
    char line[256];
    char word[64];
    char candidate[320];
    int line_h = pFont->glyph_y + 2;
    const char* p = pText;
    int i;

    line[0] = '\0';
    while (*p) {
        i = 0;
        while (*p && *p != ' ' && i < 63) {
            word[i++] = *p++;
        }
        word[i] = '\0';
        if (*p == ' ') {
            p++;
        }
        if (word[0] == '\0') {
            continue;
        }
        if (line[0]) {
            sprintf(candidate, "%s %s", line, word);
        } else {
            strcpy(candidate, word);
        }
        if (line[0] && pX + BrPixelmapTextWidth(gBack_screen, pFont, candidate) >= pRight) {
            if (pY + line_h <= pBottom) {
                TransBrPixelmapText(gBack_screen, pX, pY, pColour, pFont, line);
                pY += line_h;
            }
            line[0] = '\0';
        }
        if (line[0]) {
            strcat(line, " ");
        }
        strcat(line, word);
    }
    if (line[0] && pY + line_h <= pBottom) {
        TransBrPixelmapText(gBack_screen, pX, pY, pColour, pFont, line);
    }
}

// Load the player's character FLI portrait into gPanel_buffer[0].
static void AchLoadPlayerPortrait(void) {
    const char* flic_name = (gProgram_state.frank_or_anniness == 0) ? "FRANK.FLI" : "ANNIE.FLI";
    tU8* flic_data = NULL;
    tU32 flic_len = 0;
    AchClearPedImage();
    gAch_mugshot_opponent = -1;
    LoadFlicData((char*)flic_name, &flic_data, &flic_len);
    if (flic_data != NULL) {
        AchGetFlicDimensions(flic_data, &gAch_ped_flic_w, &gAch_ped_flic_h);
        gAch_ped_flic_data = flic_data;
        gAch_ped_flic_len = flic_len;
        ChangePanelFlic(0, flic_data, flic_len);
        EndFlic(&gPanel_flic[0]);
    }
}

static tU32 AchCalcChecksum(const tAchievement_save* s) {
    const tU8* p = (const tU8*)s;
    tU32 cs = 0;
    int i;
    for (i = 0; i < (int)(sizeof(tAchievement_save) - sizeof(tU32)); i++) {
        cs ^= (tU32)p[i] * (tU32)(i + 1);
    }
    return cs;
}

static void AchGetPath(tPath_name the_path, int slot) {
    PathCat(the_path, gApplication_path, gMeld_active ? "SAVEGAME_M" : "SAVEGAME");
    PathCat(the_path, the_path, "ACHIEVx");
    the_path[strlen(the_path) - 1] = '0' + slot;
}

void Achievements_Load(int slot) {
    tPath_name the_path;
    FILE* f;
    tU32 the_size;

    memset(&gAch_save, 0, sizeof(gAch_save));
    gAch_save.version = ACHIEVEMENT_SAVE_VERSION;
    AchGetPath(the_path, slot);
    f = DRfopen(the_path, "rb");
    if (f == NULL) return;
    the_size = GetFileLength(f);
    if (the_size == sizeof(tAchievement_save)) {
        fread(&gAch_save, 1, sizeof(tAchievement_save), f);
        if (gAch_save.version != ACHIEVEMENT_SAVE_VERSION
                || AchCalcChecksum(&gAch_save) != gAch_save.checksum) {
            memset(&gAch_save, 0, sizeof(gAch_save));
            gAch_save.version = ACHIEVEMENT_SAVE_VERSION;
        }
    }
    fclose(f);
}

void Achievements_Save(int slot) {
    tPath_name dir_path;
    tPath_name the_path;
    FILE* f;

    gAch_save.version = ACHIEVEMENT_SAVE_VERSION;
    gAch_save.checksum = AchCalcChecksum(&gAch_save);
    PathCat(dir_path, gApplication_path, gMeld_active ? "SAVEGAME_M" : "SAVEGAME");
    mkdir(dir_path, 0755);
    AchGetPath(the_path, slot);
    PDFileUnlock(the_path);
    f = DRfopen(the_path, "wb");
    if (f == NULL) return;
    fwrite(&gAch_save, 1, sizeof(tAchievement_save), f);
    fclose(f);
}

static void AchUnlock(int idx) {
    if (gAch_save.unlocked[idx]) return;
    gAch_save.unlocked[idx] = 1;
    Achievement_TriggerPopup(idx);
}

static int AchIsUnlocked(int idx) {
    return gAch_save.unlocked[idx];
}

int AchCountUnlocked(void) {
    int count = 0;
    int i;
    for (i = 0; i < ACHIEVEMENT_COUNT; i++) {
        if (AchIsUnlocked(i)) count++;
    }
    return count;
}

// Draw stats summary page into the achievement panel (gCurrent_achievement_index == -1).
static void DrawStatsPage(void) {
    int panel_left = gCurrent_graf_data->change_car_panel_left;
    int panel_top = gCurrent_graf_data->change_car_panel_top;
    int panel_right = gCurrent_graf_data->change_car_panel_right;
    int panel_bottom = gCurrent_graf_data->change_car_panel_bottom;
    int panel_w = panel_right - panel_left;
    int panel_h = panel_bottom - panel_top;
    int title_h;
    int sep_y;
    int body_top;
    int col_img_w;
    int text_left;
    int y;
    int line_h;
    int num_rows;
    int i;
    tU32 stat_values[8];
    char buf[48];

    static const struct { const char* label; } kStatRows[] = {
        { "Races Won" },
        { "  by Peds" },
        { "  by Checkpoints" },
        { "  by Opponents" },
        { "Peds Killed" },
        { "Most Peds (1 race)" },
        { "Opponents Wasted" },
        { "Powerups Collected" },
    };

    LoadFont(kFont_ORANGHED);
    title_h = gFonts[kFont_ORANGHED].height;

    gAch_panel_x = panel_left;
    gAch_panel_y = panel_top;
    gAch_panel_w = panel_w;
    gAch_panel_h = panel_h;

    BrPixelmapRectangleFill(gBack_screen, panel_left, panel_top, panel_w, panel_h, 0);

    TransDRPixelmapText(gBack_screen, panel_left + 3, panel_top + 3, &gFonts[kFont_ORANGHED],
        "Statistics", panel_right - 6);

    sep_y = panel_top + title_h + 6;
    BrPixelmapLine(gBack_screen, panel_left + 2, sep_y, panel_right - 2, sep_y, 6);
    body_top = sep_y + 4;

    col_img_w = panel_w * 2 / 5;
    if (gAch_ped_flic_w > 0 && gPanel_buffer[0] != NULL) {
        int body_h = panel_bottom - body_top;
        int src_w = gAch_ped_flic_w;
        int src_h = gAch_ped_flic_h;
        int dst_w = col_img_w;
        int dst_h = (src_w > 0) ? dst_w * src_h / src_w : body_h;
        int dst_x;
        int dst_y;
        if (dst_h > body_h) {
            dst_h = body_h;
            dst_w = (src_h > 0) ? dst_h * src_w / src_h : col_img_w;
        }
        dst_x = panel_left + (col_img_w - dst_w) / 2;
        dst_y = body_top + (body_h - dst_h) / 2;
        AchBlitScaled(gPanel_buffer[0], 0, 0, src_w, src_h,
            gBack_screen, dst_x, dst_y, dst_w, dst_h, NULL);
        text_left = panel_left + col_img_w + 4;
    } else {
        text_left = panel_left + 4;
    }

    line_h = gFont_7->glyph_y + 2;
    y = body_top + 2;

    stat_values[0] = gGame_stats.total_races_won;
    stat_values[1] = gGame_stats.races_won_by_peds;
    stat_values[2] = gGame_stats.races_won_by_checkpoints;
    stat_values[3] = gGame_stats.races_won_by_opponents;
    stat_values[4] = gGame_stats.total_peds_killed;
    stat_values[5] = gGame_stats.max_peds_single_race;
    stat_values[6] = gGame_stats.total_opponents_wasted;
    stat_values[7] = gGame_stats.total_powerups_collected;

    num_rows = (int)(sizeof(kStatRows) / sizeof(kStatRows[0]));
    for (i = 0; i < num_rows; i++) {
        int label_w;
        int val_w;
        if (y + line_h > panel_bottom - 2) break;
        TransBrPixelmapText(gBack_screen, text_left, y, 1, gFont_7, kStatRows[i].label);
        sprintf(buf, "%lu", (unsigned long)stat_values[i]);
        val_w = BrPixelmapTextWidth(gBack_screen, gFont_7, buf);
        TransBrPixelmapText(gBack_screen, panel_right - val_w - 3, y, gColours[8], gFont_7, buf);
        label_w = text_left + BrPixelmapTextWidth(gBack_screen, gFont_7, kStatRows[i].label);
        BrPixelmapLine(gBack_screen, label_w + 2, y + gFont_7->glyph_y - 2,
            panel_right - val_w - 5, y + gFont_7->glyph_y - 2, 49);
        y += line_h;
    }

    AchSavePanel(gAch_new_buf);
    if (gAch_dissolve_start >= 0) {
        AchApplyDissolve();
    }

    BrPixelmapRectangleFill(gBack_screen,
        gCurrent_graf_data->change_car_line_left,
        gCurrent_graf_data->change_car_line_y + 1,
        gCurrent_graf_data->change_car_line_right - gCurrent_graf_data->change_car_line_left,
        gCurrent_graf_data->change_car_text_y + gFont_7->glyph_y + 2 - gCurrent_graf_data->change_car_line_y,
        0);
    BrPixelmapLine(gBack_screen,
        gCurrent_graf_data->change_car_line_left,
        gCurrent_graf_data->change_car_line_y,
        gCurrent_graf_data->change_car_line_right,
        gCurrent_graf_data->change_car_line_y,
        6);

    {
        char unlocked_str[32];
        int unlocked_w;
        int text_centre_x;
        int unlocked_box_h;
        int unlocked_box_w;
        int unlocked_box_x;
        int unlocked_box_y;
        sprintf(unlocked_str, "Unlocked %d/%d", AchCountUnlocked(), ACHIEVEMENT_COUNT);
        unlocked_w = BrPixelmapTextWidth(gBack_screen, gFont_7, unlocked_str);
        text_centre_x = (125 + 221) / 2;
        unlocked_box_h = gFont_7->glyph_y + 8;
        unlocked_box_w = unlocked_w + 26;
        unlocked_box_x = text_centre_x - unlocked_box_w / 2;
        unlocked_box_y = gCurrent_graf_data->change_car_text_y + 20;
        BrPixelmapRectangleFill(gBack_screen, unlocked_box_x, unlocked_box_y, unlocked_box_w, unlocked_box_h, 0);
        TransBrPixelmapText(gBack_screen,
            text_centre_x - unlocked_w / 2,
            unlocked_box_y + 4,
            1,
            gFont_7,
            unlocked_str);
    }
}

// Added by dethrace — returns 1 and fills out_current/out_max for stats-tracked achievements.
// Only covers achievements with thresholds > 1 where progress is meaningful.
static int AchGetProgress(int idx, tU32* out_current, tU32* out_max) {
    switch (idx) {
    case 1:  *out_current = gGame_stats.total_races_won;       *out_max = 10;      return 1; // Winner Winner Chicken Killer
    case 4:  *out_current = gGame_stats.total_peds_killed;     *out_max = 100;     return 1; // Massacre
    case 5:  *out_current = gGame_stats.total_peds_killed;     *out_max = 1000;    return 1; // I'm Coming to Get You
    case 19: *out_current = gGame_stats.max_peds_single_race;  *out_max = 100;     return 1; // Going Postal
    case 20: *out_current = gGame_stats.max_peds_single_race;  *out_max = 500;     return 1; // Hunter
    case 21: *out_current = gGame_stats.total_peds_killed;     *out_max = 5000;    return 1; // Public Health Crisis
    case 22: *out_current = gGame_stats.total_peds_killed;     *out_max = 10000;   return 1; // Extinction Event
    case 26: *out_current = gGame_stats.races_won_by_peds;       *out_max = 10;     return 1; // Grim Reaper
    case 32: *out_current = gGame_stats.total_opponents_wasted; *out_max = 100;    return 1; // Scrap Dealer
    case 33: *out_current = gGame_stats.races_won_by_opponents; *out_max = 10;    return 1; // Serial Killer
    case 42: *out_current = gGame_stats.total_repair_spent;    *out_max = 1000000; return 1; // Insurance Nightmare
    case 45: *out_current = gGame_stats.total_credits_earned;  *out_max = 1000000; return 1; // Millionaire
    default: return 0;
    }
}

// Added by dethrace
static void DrawAchievement(int pCurrent_choice, int pCurrent_mode) {
    const tAchievement* ach;
    int panel_left;
    int panel_top;
    int panel_right;
    int panel_bottom;
    int panel_w;
    int panel_h;
    int title_h;
    int sep_y;
    int desc_top;
    int bar_h = 6;
    int bar_top;
    int bar_left;
    int bar_right;
    int bar_w;
    int filled;
    int count_w;
    int unlocked_w;
    int text_centre_x;
    int unlocked_box_x;
    int unlocked_box_y;
    int unlocked_box_w;
    int unlocked_box_h;
    int text_col_left;
    int text_col_right;
    int is_unlocked;
    int has_progress;
    tU32 prog_current;
    tU32 prog_max;
    int status_w;
    const char* status_str;
    char count_str[16];
    char unlocked_str[32];
    char prog_str[32];

    if (gCurrent_achievement_index < 0) {
        DrawStatsPage();
        return;
    }
    ach = &gAchievement_list[gCurrent_achievement_index];
    panel_left = gCurrent_graf_data->change_car_panel_left;
    panel_top = gCurrent_graf_data->change_car_panel_top;
    panel_right = gCurrent_graf_data->change_car_panel_right;
    panel_bottom = gCurrent_graf_data->change_car_panel_bottom;
    panel_w = panel_right - panel_left;
    panel_h = panel_bottom - panel_top;

    LoadFont(kFont_ORANGHED);
    title_h = gFonts[kFont_ORANGHED].height;
    has_progress = AchGetProgress(gCurrent_achievement_index, &prog_current, &prog_max);

    gAch_panel_x = panel_left;
    gAch_panel_y = panel_top;
    gAch_panel_w = panel_w;
    gAch_panel_h = panel_h;

    // Clear panel
    BrPixelmapRectangleFill(gBack_screen,
        panel_left,
        panel_top,
        panel_w,
        panel_h,
        0);

    // Row 1: title (full width) and N/87 count
    sprintf(count_str, "%d/%d", gCurrent_achievement_index + 1, ACHIEVEMENT_COUNT);
    count_w = BrPixelmapTextWidth(gBack_screen, gFont_7, count_str);
    TransBrPixelmapText(gBack_screen,
        panel_right - count_w - 3,
        panel_top + 3,
        1,
        gFont_7,
        count_str);
    TransDRPixelmapText(gBack_screen,
        panel_left + 3,
        panel_top + 3,
        &gFonts[kFont_ORANGHED],
        (char*)ach->name,
        panel_right - count_w - 6);

    // Separator line full panel width
    sep_y = panel_top + title_h + 6;
    BrPixelmapLine(gBack_screen, panel_left + 2, sep_y, panel_right - 2, sep_y, 6);

    // Below separator: image (left 50%) | description (right 50%)
    desc_top = sep_y + 4;
    bar_top = panel_bottom - bar_h - 4;

    if (gAch_ped_pixelmap != NULL) {
        int col_w = panel_w / 2;
        int body_h = panel_bottom - desc_top;
        int src_w = gAch_ped_pixelmap->width;
        int src_h = gAch_ped_pixelmap->height;
        int dst_w = col_w;
        int dst_h = (src_w > 0) ? dst_w * src_h / src_w : body_h;
        int dst_x;
        int dst_y;
        if (dst_h > body_h) {
            dst_h = body_h;
            dst_w = (src_h > 0) ? dst_h * src_w / src_h : col_w;
        }
        dst_x = panel_left + (col_w - dst_w) / 2;
        dst_y = desc_top + (body_h - dst_h) / 2;
        AchBlitScaled(gAch_ped_pixelmap, 0, 0, src_w, src_h,
            gBack_screen, dst_x, dst_y, dst_w, dst_h, gAch_rend2flc);
        text_col_left = panel_left + col_w + 2;
    } else if (gAch_mugshot_opponent >= 0 && gPanel_buffer[0] != NULL) {
        int col_w = panel_w / 2;
        int body_h = panel_bottom - desc_top;
        int src_w = gCurrent_graf_data->dare_mugshot_width;
        int src_h = gCurrent_graf_data->dare_mugshot_height;
        int dst_w = col_w;
        int dst_h = (src_w > 0) ? dst_w * src_h / src_w : body_h;
        int dst_x;
        int dst_y;
        if (dst_h > body_h) {
            dst_h = body_h;
            dst_w = (src_h > 0) ? dst_h * src_w / src_h : col_w;
        }
        dst_x = panel_left + (col_w - dst_w) / 2;
        dst_y = desc_top + (body_h - dst_h) / 2;
        AchBlitScaled(gPanel_buffer[0],
            gCurrent_graf_data->dare_mug_left_margin,
            gCurrent_graf_data->dare_mug_top_margin,
            src_w, src_h,
            gBack_screen, dst_x, dst_y, dst_w, dst_h, NULL);
        text_col_left = panel_left + col_w + 2;
    } else if (gAch_ped_flic_w > 0 && gPanel_buffer[0] != NULL) {
        int col_w = panel_w / 2;
        int body_h = panel_bottom - desc_top;
        int src_w = gAch_ped_flic_w;
        int src_h = gAch_ped_flic_h;
        int dst_w = col_w;
        int dst_h = (src_w > 0) ? dst_w * src_h / src_w : body_h;
        int dst_x;
        int dst_y;
        if (dst_h > body_h) {
            dst_h = body_h;
            dst_w = (src_h > 0) ? dst_h * src_w / src_h : col_w;
        }
        dst_x = panel_left + (col_w - dst_w) / 2;
        dst_y = desc_top + (body_h - dst_h) / 2;
        AchBlitScaled(gPanel_buffer[0], 0, 0, src_w, src_h,
            gBack_screen, dst_x, dst_y, dst_w, dst_h, NULL);
        text_col_left = panel_left + col_w + 2;
    } else {
        text_col_left = panel_left + 4;
    }
    text_col_right = panel_right - 4;

    if (has_progress) {
        int count_y = bar_top - gFont_7->glyph_y - 4;
        DrawWrappedText(text_col_left, desc_top, text_col_right, count_y - 2, 1, gFont_7, ach->description);
        // "X/MAX" count centred above the bar
        {
            tU32 clamped = prog_current < prog_max ? prog_current : prog_max;
            int prog_w;
            int col_centre_x = (text_col_left + text_col_right) / 2;
            sprintf(prog_str, "%lu/%lu", (unsigned long)clamped, (unsigned long)prog_max);
            prog_w = BrPixelmapTextWidth(gBack_screen, gFont_7, prog_str);
            TransBrPixelmapText(gBack_screen, col_centre_x - prog_w / 2, count_y, gColours[8], gFont_7, prog_str);
        }
    } else {
        DrawWrappedText(text_col_left, desc_top, text_col_right, panel_bottom - 2, 1, gFont_7, ach->description);
    }

    // Progress bar
    if (has_progress) {
        int pct = (prog_current >= prog_max) ? 100 : (int)(prog_current * 100 / prog_max);
        bar_left = text_col_left;
        bar_right = text_col_right;
        bar_w = bar_right - bar_left;
        filled = bar_w * pct / 100;
        if (filled > 0) {
            BrPixelmapRectangleFill(gBack_screen, bar_left, bar_top, filled, bar_h, 49);
        }
        if (filled < bar_w) {
            BrPixelmapRectangleFill(gBack_screen, bar_left + filled, bar_top, bar_w - filled, bar_h, 5);
        }
        DrawRectangle(gBack_screen, bar_left - 1, bar_top - 1, bar_right, bar_top + bar_h, 6);
    }

    AchSavePanel(gAch_new_buf);
    if (gAch_dissolve_start >= 0) {
        AchApplyDissolve();
    }

    // Erase the "Change car" flic's sunken text box
    BrPixelmapRectangleFill(gBack_screen,
        gCurrent_graf_data->change_car_line_left,
        gCurrent_graf_data->change_car_line_y + 1,
        gCurrent_graf_data->change_car_line_right - gCurrent_graf_data->change_car_line_left,
        gCurrent_graf_data->change_car_text_y + gFont_7->glyph_y + 2 - gCurrent_graf_data->change_car_line_y,
        0);

    sprintf(unlocked_str, "Unlocked %d/%d", AchCountUnlocked(), ACHIEVEMENT_COUNT);
    unlocked_w = BrPixelmapTextWidth(gBack_screen, gFont_7, unlocked_str);
    // Horizontal centre of the gap between the two buttons (x=125 to x=221 in 320x200)
    text_centre_x = (125 + 221) / 2;
    unlocked_box_h = gFont_7->glyph_y + 8;
    unlocked_box_w = unlocked_w + 26;
    unlocked_box_x = text_centre_x - unlocked_box_w / 2;
    unlocked_box_y = gCurrent_graf_data->change_car_text_y + 20;
    BrPixelmapRectangleFill(gBack_screen, unlocked_box_x, unlocked_box_y, unlocked_box_w, unlocked_box_h, 0);
    TransBrPixelmapText(gBack_screen,
        text_centre_x - unlocked_w / 2,
        unlocked_box_y + 4,
        1,
        gFont_7,
        unlocked_str);

    BrPixelmapLine(gBack_screen,
        gCurrent_graf_data->change_car_line_left,
        gCurrent_graf_data->change_car_line_y,
        gCurrent_graf_data->change_car_line_right,
        gCurrent_graf_data->change_car_line_y,
        6);

    // LOCKED / UNLOCKED status for this achievement, centred below the line
    is_unlocked = AchIsUnlocked(gCurrent_achievement_index);
    status_str = is_unlocked ? "UNLOCKED" : "LOCKED";
    status_w = BrPixelmapTextWidth(gBack_screen, gFont_7, status_str);
    TransBrPixelmapText(gBack_screen,
        text_centre_x - status_w / 2,
        gCurrent_graf_data->change_car_text_y,
        is_unlocked ? 201 : 44,
        gFont_7,
        (char*)status_str);
}

// Added by dethrace
static void StartAchievementView(void) {
    AchUpdateMugShot();
    DrawAchievement(0, 0);
}

// Added by dethrace
static int UpAchievement(int* pCurrent_choice, int* pCurrent_mode) {
    AddToFlicQueue(gStart_interface_spec->pushed_flics[2].flic_index,
        gStart_interface_spec->pushed_flics[2].x[gGraf_data_index],
        gStart_interface_spec->pushed_flics[2].y[gGraf_data_index],
        1);
    DRS3StartSound(gEffects_outlet, 3000);
    if (gCurrent_achievement_index > -1) {
        RemoveTransientBitmaps(1);
        AchStartDissolve();
        gCurrent_achievement_index--;
        AchUpdateMugShot();
    }
    return 0;
}

// Added by dethrace
static int DownAchievement(int* pCurrent_choice, int* pCurrent_mode) {
    AddToFlicQueue(gStart_interface_spec->pushed_flics[3].flic_index,
        gStart_interface_spec->pushed_flics[3].x[gGraf_data_index],
        gStart_interface_spec->pushed_flics[3].y[gGraf_data_index],
        1);
    DRS3StartSound(gEffects_outlet, 3000);
    if (gCurrent_achievement_index < ACHIEVEMENT_COUNT - 1) {
        RemoveTransientBitmaps(1);
        AchStartDissolve();
        gCurrent_achievement_index++;
        AchUpdateMugShot();
    }
    return 0;
}

// Added by dethrace
static int UpClickAchievement(int* pCurrent_choice, int* pCurrent_mode, int pX_offset, int pY_offset) {
    UpAchievement(pCurrent_choice, pCurrent_mode);
    return 0;
}

// Added by dethrace
static int DownClickAchievement(int* pCurrent_choice, int* pCurrent_mode, int pX_offset, int pY_offset) {
    DownAchievement(pCurrent_choice, pCurrent_mode);
    return 0;
}

// Added by dethrace — called from gameplay code when an opponent is wasted
void Achievement_OnOpponentWasted(const char* pOpponent_name, int opponent_idx) {
    tU32 now = PDGetTotalTime();
    int i;
    Stats_OnOpponentWasted();
    gAch_race_opponents_wasted++;
    // Named opponent achievements
    for (i = 0; i < ACHIEVEMENT_COUNT; i++) {
        if (gAchievement_list[i].trigger == eAchTrigger_opponent_wasted
                && gAchievement_list[i].trigger_param != NULL
                && strcasecmp(gAchievement_list[i].trigger_param, pOpponent_name) == 0) {
            AchUnlock(i);
        }
    }
    // Cop Killer
    if (strcasecmp(pOpponent_name, "COP") == 0) AchUnlock(ACHIEVEMENT_COP_KILLER);
    // Generic opponent milestones
    if (gGame_stats.total_opponents_wasted >= 1) AchUnlock(ACHIEVEMENT_FIRST_BLOOD);
    if (gGame_stats.total_opponents_wasted >= 100) AchUnlock(ACHIEVEMENT_SCRAP_DEALER);
    if (gAch_race_opponents_wasted >= 3) AchUnlock(ACHIEVEMENT_ROAD_RAGE);
    // Demolition Derby: 2 opponents within 30s
    if (gAch_last_opponent_kill_time != 0 && now - gAch_last_opponent_kill_time <= 30000) {
        AchUnlock(ACHIEVEMENT_DEMOLITION_DERBY);
    }
    gAch_last_opponent_kill_time = now;
    // Not Mine: this opponent was mine-hit by player proximity recently
    if (gAch_mine_flagged_idx == opponent_idx && gAch_mine_flag_time != 0
            && now - gAch_mine_flag_time <= kMine_flag_ms) {
        AchUnlock(ACHIEVEMENT_NOT_MINE);
    }
}

// Added by dethrace — called after Stats_OnPedKilled
void Achievement_OnPedKilled(int is_footballer) {
    if (gAch_race_player_peds_killed == 0) {
        gAch_patient_zero_blocked = 1;
    }
    if (is_footballer) {
        int i, s, f;
        if (gAch_footballer_total < 0) {
            gAch_footballer_total = 0;
            for (i = 0; i < gPed_count; i++) {
                tPedestrian_data* ped = &gPedestrian_array[i];
                int found = 0;
                if (ped->sequences == NULL) continue;
                for (s = 0; s < ped->number_of_sequences && !found; s++) {
                    for (f = 0; f < ped->sequences[s].number_of_frames && !found; f++) {
                        br_pixelmap* pm = ped->sequences[s].frames[f].pixelmap;
                        if (pm != NULL && pm->identifier != NULL
                                && (strncasecmp(pm->identifier, "SKN", 3) == 0
                                    || strncasecmp(pm->identifier, "COW", 3) == 0)) {
                            found = 1;
                        }
                    }
                }
                if (found) gAch_footballer_total++;
            }
            LOG_DEBUG("Not much of a sports fan: %d footballer peds on this map", gAch_footballer_total);
        }
        gAch_footballer_kills++;
        LOG_DEBUG("Not much of a sports fan: %d/%d footballers killed", gAch_footballer_kills, gAch_footballer_total);
        if (gAch_footballer_total > 0 && gAch_footballer_kills >= gAch_footballer_total) {
            AchUnlock(ACHIEVEMENT_NOT_MUCH_OF_A_SPORTS_FAN);
        }
    }
}

// Added by dethrace — called after Stats_OnRaceResult
void Achievement_OnRaceResult(tRace_over_reason reason) {
    int race_won = (reason == eRace_over_laps || reason == eRace_over_peds || reason == eRace_over_opponents);
    int i, cars_driven, found;
    if (gGame_stats.total_races_won >= 1) AchUnlock(ACHIEVEMENT_RACER);
    if (gGame_stats.total_races_won >= 10) AchUnlock(ACHIEVEMENT_WINNER_WINNER);
    if (reason == eRace_over_laps) AchUnlock(ACHIEVEMENT_RACIST);
    if (reason == eRace_over_peds) AchUnlock(ACHIEVEMENT_PEDICIDE);
    if (gGame_stats.races_won_by_peds >= 10) AchUnlock(ACHIEVEMENT_GRIM_REAPER);
    if (reason == eRace_over_opponents) AchUnlock(ACHIEVEMENT_I_WAS_IN_THE_WAR);
    if (gGame_stats.races_won_by_opponents >= 10) AchUnlock(ACHIEVEMENT_SERIAL_KILLER);
    // Pacifist: won a race without personally killing any pedestrians (arena races excluded)
    if (race_won && gAch_race_player_peds_killed == 0 && !Meld_IsArenaTrack()) {
        AchUnlock(ACHIEVEMENT_PACIFIST);
    }
    // Test Drive: complete a race in 10 different cars
    if (race_won && gProgram_state.car_name[0] != '\0') {
        found = 0;
        cars_driven = 0;
        for (i = 0; i < ACHIEVEMENT_CAR_SLOTS; i++) {
            if (gAch_save.cars_raced[i][0] == '\0') break;
            cars_driven++;
            if (strncasecmp(gAch_save.cars_raced[i], gProgram_state.car_name, ACHIEVEMENT_CAR_NAME_LEN - 1) == 0) {
                found = 1;
            }
        }
        if (!found && cars_driven < ACHIEVEMENT_CAR_SLOTS) {
            strncpy(gAch_save.cars_raced[cars_driven], gProgram_state.car_name, ACHIEVEMENT_CAR_NAME_LEN - 1);
            gAch_save.cars_raced[cars_driven][ACHIEVEMENT_CAR_NAME_LEN - 1] = '\0';
            cars_driven++;
        }
        if (cars_driven >= 10) AchUnlock(ACHIEVEMENT_TEST_DRIVE);
    }
    // Total Wipeout: wrecked every opponent in the race
    if (gAch_race_start_opponents > 0 && gAch_race_opponents_wasted >= gAch_race_start_opponents) {
        AchUnlock(ACHIEVEMENT_TOTAL_WIPEOUT);
    }
    // Credit Where It's Due: earned 100k credits in this race
    if (gGame_stats.total_credits_earned - gAch_credits_race_start >= 100000) {
        AchUnlock(ACHIEVEMENT_CREDIT_WHERE_ITS_DUE);
    }
    // My Car Now: won a race in a stolen opponent's car (car_number 100/101 = starting cars)
    if (race_won) {
        int cn = gOpponents[gProgram_state.current_car.index].car_number;
        if (cn != 100 && cn != 101) {
            AchUnlock(ACHIEVEMENT_MY_CAR_NOW);
        }
    }
}

// Added by dethrace — called at race start to reset per-race counters
void Achievement_OnRaceStart(void) {
    int i;
    gAch_race_opponents_wasted = 0;
    gAch_race_player_peds_killed = 0;
    gAch_race_cow_kills = 0;
    gAch_consecutive_player_kills = 0;
    gAch_last_player_kill_time = 0;
    gAch_kill_count = 0;
    gAch_kill_head = 0;
    memset(gAch_pinball_objects, 0, sizeof(gAch_pinball_objects));
    gAch_pinball_obj_count = 0;
    gAch_proxray_kills = 0;
    gAch_last_opponent_kill_time = 0;
    gAch_mine_flagged_idx = -1;
    gAch_mine_flag_time = 0;
    gAch_airborne_ms = 0;
    gAch_prev_wheels_on_ground = 0;
    gAch_confirmed_on_ground = 0;
    gAch_liftoff_damage = 0;
    gAch_liftoff_ms = 0;
    gAch_skid_distance_m = 0.0f;
    gAch_race_start_opponents = gCurrent_race.number_of_racers;
    gAch_credits_race_start = gGame_stats.total_credits_earned;
    memset(gAch_powerup_seen, 0, sizeof(gAch_powerup_seen));
    gAch_race_unique_powerups = 0;

    gAch_patient_zero_blocked = 0;

    gAch_footballer_total = -1; // -1 = not yet counted (peds not loaded at race start)
    gAch_footballer_kills = 0;

    // Discard any leftover popups from the previous race
    {
        int i;
        for (i = 0; i < gAch_popup_count; i++) {
            AchFreeSlot(&gAch_popup_slots[i]);
        }
        gAch_popup_count = 0;
        gAch_popup_dismissing = 0;
    }
}

// Added by dethrace — called after Stats_OnCreditsEarned
void Achievement_OnCreditsEarned(tU32 amount) {
    if (amount == 0) return;
    if (gGame_stats.total_credits_earned >= 1000000) AchUnlock(ACHIEVEMENT_MILLIONAIRE);
}

// Added by dethrace — called when the local player kills a ped via car collision or physics object
void Achievement_OnPlayerPedKilled(float speed_mph, int is_cow, int is_billiards, void* billiards_actor, int is_footballer, int is_proximity_rayed) {
    tU32 now = PDGetTotalTime();
    int i, found, n;

    gAch_race_player_peds_killed++;
    {
        tU32 total = gGame_stats.total_peds_killed;
        if (total >= 1) AchUnlock(ACHIEVEMENT_HIT_N_RUN);
        if (gAch_race_player_peds_killed == 1 && !gAch_patient_zero_blocked) AchUnlock(ACHIEVEMENT_PATIENT_ZERO);
        if (total >= 100) AchUnlock(ACHIEVEMENT_MASSACRE);
        if (total >= 1000) AchUnlock(ACHIEVEMENT_IM_COMING_TO_GET_YOU);
        if (total >= 5000) AchUnlock(ACHIEVEMENT_PUBLIC_HEALTH_CRISIS);
        if (total >= 10000) AchUnlock(ACHIEVEMENT_EXTINCTION_EVENT);
        if (gGame_stats.max_peds_single_race >= 100) AchUnlock(ACHIEVEMENT_GOING_POSTAL);
        if (gGame_stats.max_peds_single_race >= 500) AchUnlock(ACHIEVEMENT_HUNTER);
    }

    // Speed-based (direct car hits only — billiards speed is the object's, not the car's)
    if (!is_billiards) {
        if (speed_mph < 5.0f) AchUnlock(ACHIEVEMENT_SPEED_BUMP);
        if (speed_mph > 150.0f) AchUnlock(ACHIEVEMENT_ROAD_PIZZA);
    }

    // Environmental Hazard and Pinball Wizard
    if (is_billiards) {
        AchUnlock(ACHIEVEMENT_ENVIRONMENTAL_HAZARD);
        if (gPinball_factor != 0.f) {
            found = 0;
            for (i = 0; i < gAch_pinball_obj_count; i++) {
                if (gAch_pinball_objects[i].actor == billiards_actor) {
                    gAch_pinball_objects[i].count++;
                    if (gAch_pinball_objects[i].count >= 5) AchUnlock(ACHIEVEMENT_PINBALL_WIZARD);
                    found = 1;
                    break;
                }
            }
            if (!found && gAch_pinball_obj_count < kPinball_track_max) {
                gAch_pinball_objects[gAch_pinball_obj_count].actor = billiards_actor;
                gAch_pinball_objects[gAch_pinball_obj_count].count = 1;
                gAch_pinball_obj_count++;
            }
        }
    }

    // Cow achievements
    if (is_cow) {
        AchUnlock(ACHIEVEMENT_BOVINE_INTERVENTION);
        gAch_race_cow_kills++;
        if (gAch_race_cow_kills >= 30) AchUnlock(ACHIEVEMENT_WHOLE_HERD);
    }

    // Lawn Mower: 20 consecutive kills with no more than 10s between each
    if (gAch_last_player_kill_time != 0 && now - gAch_last_player_kill_time > kLawnMower_reset_ms) {
        gAch_consecutive_player_kills = 0;
    }
    gAch_consecutive_player_kills++;
    gAch_last_player_kill_time = now;
    if (gAch_consecutive_player_kills >= 20) AchUnlock(ACHIEVEMENT_LAWN_MOWER);

    // Rolling kill window for Turkey / Rampage
    gAch_kill_times[gAch_kill_head] = now;
    gAch_kill_head = (gAch_kill_head + 1) % kKillWindow_max;
    if (gAch_kill_count < kKillWindow_max) gAch_kill_count++;

    n = 0;
    for (i = 0; i < gAch_kill_count; i++) {
        if (now - gAch_kill_times[i] <= 500) n++;
    }
    if (n >= 3) AchUnlock(ACHIEVEMENT_TURKEY);

    n = 0;
    for (i = 0; i < gAch_kill_count; i++) {
        if (now - gAch_kill_times[i] <= 10000) n++;
    }
    if (n >= 10) AchUnlock(ACHIEVEMENT_RAMPAGE);

    // Zap: 50 kills during one electro bastard ray activation
    if (is_proximity_rayed) {
        gAch_proxray_kills++;
        if (gAch_proxray_kills >= 50) AchUnlock(ACHIEVEMENT_ZAP);
    }
}

// Added by dethrace — called when a car activates the electro bastard ray
void Achievement_OnProxRayStart(tCar_spec* pCar) {
    if (pCar->driver != eDriver_local_human) return;
    gAch_proxray_kills = 0;
}

// Added by dethrace — called when the electro bastard ray powerup ends
void Achievement_OnProxRayEnd(tCar_spec* pCar) {
    if (pCar->driver != eDriver_local_human) return;
    gAch_proxray_kills = 0;
}

// Added by dethrace — called after Stats_OnRepairSpent
void Achievement_OnRepairSpent(void) {
    if (gGame_stats.total_repair_spent >= 1000000) AchUnlock(ACHIEVEMENT_INSURANCE_NIGHTMARE);
}

// Added by dethrace — called from GotPowerupX when a car collects a powerup
void Achievement_OnPowerupCollected(tCar_spec* pCar, int powerup_index) {
    if (pCar->driver != eDriver_local_human) return;
    if (powerup_index >= 0 && powerup_index < kPowerup_seen_max_idx
            && !gAch_powerup_seen[powerup_index]) {
        gAch_powerup_seen[powerup_index] = 1;
        gAch_race_unique_powerups++;
        if (gAch_race_unique_powerups >= 10) AchUnlock(ACHIEVEMENT_POWER_HUNGRY);
    }
}

// Added by dethrace — called when the player's car falls off the map
void Achievement_OnFallenOffWorld(void) {
    AchUnlock(ACHIEVEMENT_NULL_AND_VOID);
}

// Added by dethrace — called every frame from mainloop for per-frame achievement tracking
void Achievement_OnPlayerCarFrame(tCar_spec* pCar) {
    int i, airborne, total_damage;
    float speed_mph;
    br_vector3 diff;

    if (pCar->driver != eDriver_local_human) return;

    airborne = (pCar->number_of_wheels_on_ground == 0);
    speed_mph = pCar->speedo_speed * WORLD_SCALE / 1600.0f * 3600000.0f;

    // Frequent Flyer: 5 seconds continuous airborne
    if (airborne) {
        gAch_airborne_ms += gFrame_period;
        if (gAch_airborne_ms >= 5000) AchUnlock(ACHIEVEMENT_FREQUENT_FLYER);
    } else {
        gAch_airborne_ms = 0;
    }

    // High Flyer: launch and land without taking damage, min 500ms in air
    if (!airborne) gAch_confirmed_on_ground = 1;
    if (airborne && gAch_prev_wheels_on_ground > 0 && gAch_confirmed_on_ground) {
        // just left the ground — snapshot current total damage
        gAch_liftoff_damage = 0;
        for (i = 0; i < 12; i++) gAch_liftoff_damage += pCar->damage_units[i].damage_level;
        gAch_liftoff_ms = PDGetTotalTime();
    } else if (!airborne && gAch_prev_wheels_on_ground == 0 && gAch_liftoff_ms != 0) {
        // just touched down — check if we were airborne long enough and took no damage
        if (PDGetTotalTime() - gAch_liftoff_ms >= kHighFlyer_min_air_ms) {
            total_damage = 0;
            for (i = 0; i < 12; i++) total_damage += pCar->damage_units[i].damage_level;
            if (total_damage <= gAch_liftoff_damage) AchUnlock(ACHIEVEMENT_HIGH_FLYER);
        }
        gAch_liftoff_ms = 0;
    }
    gAch_prev_wheels_on_ground = pCar->number_of_wheels_on_ground;

    // Skid Mark: continuous skid for 100m (gFrame_period * speedo_speed * WORLD_SCALE ≈ metres)
    if (pCar->new_skidding != 0) {
        gAch_skid_distance_m += (float)gFrame_period * pCar->speedo_speed * WORLD_SCALE;
        if (gAch_skid_distance_m >= 100.0f) AchUnlock(ACHIEVEMENT_SKID_MARK);
    } else {
        gAch_skid_distance_m = 0.0f;
    }

    // Close Shave: within 1m of a live ped at over 100mph
    if (speed_mph > 100.0f) {
        for (i = 0; i < gNumber_of_pedestrians; i++) {
            tPedestrian_data* ped = &gPedestrian_array[i];
            if (ped->actor->parent == gDont_render_actor) continue;
            BrVector3Sub(&diff, &ped->actor->t.t.translate.t, &pCar->car_master_actor->t.t.translate.t);
            if (BrVector3LengthSquared(&diff) < WORLD_SCALE * WORLD_SCALE) {
                AchUnlock(ACHIEVEMENT_CLOSE_SHAVE);
                break;
            }
        }
    }
}

// Added by dethrace — called from StealCar after the car is added to cars_available
void Achievement_OnCarStolen(void) {
    int i, total = 0, owned = 0;
    for (i = 0; i < gNumber_of_racers; i++) {
        int cn = gOpponents[i].car_number;
        if (cn != 100 && cn != 101) total++;
    }
    for (i = 0; i < gProgram_state.number_of_cars; i++) {
        int idx = gProgram_state.cars_available[i];
        int cn = gOpponents[idx].car_number;
        if (cn != 100 && cn != 101) owned++;
    }
    if (total > 0 && owned >= total) AchUnlock(ACHIEVEMENT_COLLECTOR);
}

// Added by dethrace — called from HitMine when a car takes mine damage
void Achievement_OnMineHit(tCar_spec* pCar) {
    br_vector3 diff;
    br_scalar dist_sq;
    if (pCar->driver == eDriver_local_human) return;
    BrVector3Sub(&diff, &pCar->car_master_actor->t.t.translate.t, &gProgram_state.current_car.car_master_actor->t.t.translate.t);
    dist_sq = BrVector3LengthSquared(&diff);
    if (dist_sq <= 10.0f * WORLD_SCALE * 10.0f * WORLD_SCALE) {
        gAch_mine_flagged_idx = pCar->index;
        gAch_mine_flag_time = PDGetTotalTime();
    }
}

// Added by dethrace — called from MungeRankEtc after game_completed is set
void Achievement_OnAllRacesDone(int game_completed) {
    if (!game_completed) return;
    AchUnlock(ACHIEVEMENT_MOST_WANTED);
}

// Added by dethrace
void DoViewAchievements(void) {
    static tFlicette flicker_on[4] = {
        { 43, { 60, 120 }, { 154, 370 } },
        { 43, { 221, 442 }, { 154, 370 } },
        { 221, { 30, 60 }, { 78, 187 } },
        { 221, { 30, 60 }, { 78, 187 } },
    };
    static tFlicette flicker_off[4] = {
        { 42, { 60, 120 }, { 154, 370 } },
        { 42, { 221, 442 }, { 154, 370 } },
        { 220, { 30, 60 }, { 78, 187 } },
        { 220, { 30, 60 }, { 78, 187 } },
    };
    static tFlicette push[4] = {
        { 154, { 60, 120 }, { 154, 370 } },
        { 45, { 221, 442 }, { 154, 370 } },
        { 222, { 30, 60 }, { 78, 187 } },
        { 225, { 30, 60 }, { 118, 283 } },
    };
    static tMouse_area mouse_areas[4] = {
        { { 60, 120 }, { 154, 370 }, { 125, 250 }, { 174, 418 }, 0, 0, 0, NULL },
        { { 221, 442 }, { 154, 370 }, { 286, 572 }, { 174, 418 }, 1, 0, 0, NULL },
        { { 30, 60 }, { 78, 187 }, { 45, 90 }, { 104, 250 }, -1, 0, 0, UpClickAchievement },
        { { 30, 60 }, { 118, 283 }, { 45, 90 }, { 145, 348 }, -1, 0, 0, DownClickAchievement },
    };
    static tInterface_spec interface_spec = {
        0, 236, 62, 0, 0, 0, -1,
        { -1, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { NULL, NULL },
        { -1, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { NULL, NULL },
        { -1, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { UpAchievement, NULL },
        { -1, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { DownAchievement, NULL },
        { 1, 1 }, { NULL, NULL }, { 1, 1 }, { NULL, NULL },
        NULL, DrawAchievement, 0, NULL, StartAchievementView, NULL, 0, { 0, 0 },
        NULL, 1, 1, COUNT_OF(flicker_on), flicker_on, flicker_off, push,
        COUNT_OF(mouse_areas), mouse_areas, 0, NULL
    };

    gCurrent_achievement_index = -1;
    gAch_mugshot_opponent = -1;
    gAch_dissolve_start = -1;
    BuildColourTable(gRender_palette);
    gStart_interface_spec = &interface_spec;

    // Build once: remap from gRender_palette indices (used by pedestrian PIX sprites) to
    // the nearest colour in gFlic_palette (the menu display palette).
    AchBuildRend2FlcRemap();
    AchBuildRgbToFlicTable();

    // Panel slot 0 used for opponent mug shots; sized to hold the full dare-screen FLI
    InitialiseFlicPanel(0,
        gCurrent_graf_data->start_race_panel_left,
        gCurrent_graf_data->start_race_panel_top,
        gCurrent_graf_data->start_race_panel_right - gCurrent_graf_data->start_race_panel_left,
        gCurrent_graf_data->start_race_panel_bottom - gCurrent_graf_data->start_race_panel_top);

    DoInterfaceScreen(&interface_spec, 0, 0);

    DisposeFlicPanel(0);
    AchClearPedImage();
}

// Added by dethrace — trigger an in-race achievement popup for a specific achievement index.
void Achievement_TriggerPopup(int idx) {
    tAch_popup_slot* slot;
    if (idx < 0 || idx >= ACHIEVEMENT_COUNT) return;
    if (!gAch_rend_table_built) {
        AchBuildRgbToRendTable();
    }
    if (gAch_popup_count == kPopup_max_stack) {
        AchFreeSlot(&gAch_popup_slots[0]);
        memmove(&gAch_popup_slots[0], &gAch_popup_slots[1],
            (kPopup_max_stack - 1) * sizeof(tAch_popup_slot));
        gAch_popup_count--;
    }
    slot = &gAch_popup_slots[gAch_popup_count];
    slot->ach_idx = idx;
    slot->pixelmap = NULL;
    slot->mug_pm = NULL;
    slot->add_time = PDGetTotalTime();
    slot->dismiss_time = 0;
    AchLoadSlotImage(slot, idx);
    gAch_popup_count++;
    gAch_popup_last_add_time = PDGetTotalTime();
    // Cancel any in-progress dismiss so the new arrival restarts the 8s timer
    if (gAch_popup_dismissing) {
        int j;
        gAch_popup_dismissing = 0;
        for (j = 0; j < gAch_popup_count; j++) {
            gAch_popup_slots[j].dismiss_time = 0;
        }
    }
}

// Added by dethrace — trigger an in-race popup for a pseudo-random achievement (for testing).
void Achievement_TriggerRandomPopup(void) {
    Achievement_TriggerPopup((int)(PDGetTotalTime() >> 3) % ACHIEVEMENT_COUNT);
}

// Draw one popup slot at (px, py) with dissolve-in based on time since it was added.
static void AchDrawOnePopup(tAch_popup_slot* slot, int px, int py, int pw, int ph,
    tU32 now, int is_rgb565, int bytes_pp,
    int text_col_white, int text_col_orange, int text_col_grey, int sep_col) {
    int add_elapsed = (int)(now - slot->add_time);
    int dissolve_in = add_elapsed < kPopup_in_ms;
    int dissolve_out_elapsed = slot->dismiss_time ? (int)(now - slot->dismiss_time) : -1;
    int dissolve_out = dissolve_out_elapsed >= 0 && dissolve_out_elapsed < kPopup_in_ms;
    int title_h, sep_y, body_top, body_h;
    int col_w, dst_w, dst_h, img_x, img_y, src_w, src_h;
    int name_x, name_right;
    int save_w, save_h;
    int line_h, title_text_w, name_lines;
    int progress, x, y;
    tU8* screen_row;
    const tU8* save_ptr;
    const tAchievement* ach = &gAchievement_list[slot->ach_idx];

    save_w = (pw < kPopup_save_max_w) ? pw : kPopup_save_max_w;
    save_h = (ph < kPopup_save_max_h) ? ph : kPopup_save_max_h;

    if (dissolve_in || dissolve_out) {
        for (y = 0; y < save_h; y++) {
            memcpy(gAch_popup_save_buf + y * kPopup_save_max_w * bytes_pp,
                (tU8*)gBack_screen->pixels + (py + y) * gBack_screen->row_bytes + px * bytes_pp,
                save_w * bytes_pp);
        }
    }

    DimRectangle(gBack_screen, px, py, px + pw, py + ph, 0);

    line_h = gFont_7->glyph_y + 2;
    title_h = gFont_7->glyph_y + 6;
    title_text_w = BrPixelmapTextWidth(gBack_screen, gFont_7, "Achievement unlocked!");
    TransBrPixelmapText(gBack_screen, px + (pw - title_text_w) / 2, py + 3, text_col_orange, gFont_7, "Achievement unlocked!");

    sep_y = py + 2 + title_h;
    BrPixelmapLine(gBack_screen, px + 1, sep_y, px + pw - 2, sep_y, sep_col);

    body_top = sep_y + 2;
    body_h = (py + ph - 2) - body_top;
    if (body_h <= 0) body_h = 1;

    if (slot->pixelmap != NULL && gAch_rend_table_built && body_h > 2) {
        col_w = pw * 2 / 5;
        src_w = slot->pixelmap->width;
        src_h = slot->pixelmap->height;
        dst_w = col_w - 1;
        dst_h = (src_w > 0) ? dst_w * src_h / src_w : body_h;
        if (dst_h > body_h) { dst_h = body_h; dst_w = (src_h > 0) ? dst_h * src_w / src_h : col_w - 1; }
        img_x = px + 1 + (col_w - 1 - dst_w) / 2;
        img_y = body_top + (body_h - dst_h) / 2;
        if (dst_w > 0 && dst_h > 0) {
            PopupBlitScaled(slot->pixelmap, 0, 0, src_w, src_h, gBack_screen, img_x, img_y, dst_w, dst_h);
        }
        name_x = px + col_w + 2;
        name_right = px + pw - 2;
    } else if (slot->mug_pm != NULL && gAch_rend_table_built && body_h > 2) {
        col_w = pw * 2 / 5;
        src_w = slot->mug_sw;
        src_h = slot->mug_sh;
        dst_w = col_w - 1;
        dst_h = (src_w > 0) ? dst_w * src_h / src_w : body_h;
        if (dst_h > body_h) { dst_h = body_h; dst_w = (src_h > 0) ? dst_h * src_w / src_h : col_w - 1; }
        img_x = px + 1 + (col_w - 1 - dst_w) / 2;
        img_y = body_top + (body_h - dst_h) / 2;
        if (dst_w > 0 && dst_h > 0) {
            PopupBlitScaledMug(slot->mug_pm, slot->mug_sx, slot->mug_sy, src_w, src_h,
                gBack_screen, img_x, img_y, dst_w, dst_h);
        }
        name_x = px + col_w + 2;
        name_right = px + pw - 2;
    } else {
        name_x = px + 3;
        name_right = px + pw - 3;
    }

    name_lines = (BrPixelmapTextWidth(gBack_screen, gFont_7, ach->name) / (name_right - name_x - 1)) + 1;
    if (name_lines > 2) name_lines = 2;
    DrawWrappedText(name_x, body_top + 1, name_right, body_top + 1 + name_lines * line_h, text_col_white, gFont_7, ach->name);
    DrawWrappedText(name_x, body_top + 1 + name_lines * line_h, name_right, py + ph - 2, text_col_grey, gFont_7, ach->description);

    if (dissolve_in) {
        progress = add_elapsed * 16 / kPopup_in_ms;
        for (y = 0; y < save_h; y++) {
            screen_row = (tU8*)gBack_screen->pixels + (py + y) * gBack_screen->row_bytes + px * bytes_pp;
            save_ptr = gAch_popup_save_buf + y * kPopup_save_max_w * bytes_pp;
            for (x = 0; x < save_w; x++) {
                if (kBayer4[y & 3][x & 3] >= (tU8)progress)
                    memcpy(screen_row + x * bytes_pp, save_ptr + x * bytes_pp, bytes_pp);
            }
        }
    } else if (dissolve_out) {
        progress = dissolve_out_elapsed * 16 / kPopup_in_ms;
        for (y = 0; y < save_h; y++) {
            screen_row = (tU8*)gBack_screen->pixels + (py + y) * gBack_screen->row_bytes + px * bytes_pp;
            save_ptr = gAch_popup_save_buf + y * kPopup_save_max_w * bytes_pp;
            for (x = 0; x < save_w; x++) {
                if (kBayer4[y & 3][x & 3] < (tU8)progress)
                    memcpy(screen_row + x * bytes_pp, save_ptr + x * bytes_pp, bytes_pp);
            }
        }
    }
}

// Added by dethrace — draw the in-race popup stack; call from RenderAFrame after DrawPowerups.
void Achievement_DrawPopup(void) {
    int i;
    tU32 now;
    int total_w, total_h, ws_off, is_rgb565, bytes_pp;
    int pw, ph, px, py, slot_py;
    int grey_idx, text_col_white, text_col_orange, text_col_grey, sep_col;

    if (gAch_popup_count == 0) return;

    now = PDGetTotalTime();

    // Start dismiss phase 8s after the last achievement was added
    if (!gAch_popup_dismissing && (int)(now - gAch_popup_last_add_time) >= kPopup_show_ms) {
        gAch_popup_dismissing = 1;
        // Schedule dissolve-out per slot: topmost (count-1) first, staggered by interval
        for (i = 0; i < gAch_popup_count; i++) {
            gAch_popup_slots[i].dismiss_time = now + (tU32)((gAch_popup_count - 1 - i) * kPopup_dismiss_interval_ms);
        }
    }

    // Remove slots whose dissolve-out has completed, from the top down
    if (gAch_popup_dismissing) {
        while (gAch_popup_count > 0) {
            tAch_popup_slot* top = &gAch_popup_slots[gAch_popup_count - 1];
            if (top->dismiss_time && (int)(now - top->dismiss_time) >= kPopup_in_ms) {
                AchFreeSlot(top);
                gAch_popup_count--;
            } else {
                break;
            }
        }
        if (gAch_popup_count == 0) return;
    }

    total_w = gGraf_specs[gGraf_spec_index].total_width;
    total_h = gGraf_specs[gGraf_spec_index].total_height;
    ws_off = WsScreenOffsetX();
    is_rgb565 = (gBack_screen->type == BR_PMT_RGB_565);
    bytes_pp = is_rgb565 ? 2 : 1;

    pw = total_w / 4;
    ph = pw * 9 / 16;
    px = total_w - pw - 4 + ws_off;
    py = total_h - ph - 4;
    if (px < 0) px = 0;
    if (px + pw > gBack_screen->width) pw = gBack_screen->width - px;
    if (pw <= 0 || ph <= 0) return;

    grey_idx = gAch_rgb5_to_rend[20 * 1024 + 20 * 32 + 20];
    text_col_white  = is_rgb565 ? (int)PaletteEntry16Bit(gRender_palette, gColours[1]) : gColours[1];
    text_col_orange = is_rgb565 ? (int)PaletteEntry16Bit(gRender_palette, gColours[8]) : gColours[8];
    text_col_grey   = is_rgb565 ? (int)PaletteEntry16Bit(gRender_palette, grey_idx) : grey_idx;
    sep_col = text_col_grey;

    // Draw bottom-up: slot 0 at base position, each successive slot above it
    for (i = 0; i < gAch_popup_count; i++) {
        slot_py = py - i * (ph + 2);
        if (slot_py < 0 || slot_py + ph > gBack_screen->height) continue;
        AchDrawOnePopup(&gAch_popup_slots[i], px, slot_py, pw, ph,
            now, is_rgb565, bytes_pp,
            text_col_white, text_col_orange, text_col_grey, sep_col);
    }
}

#endif // DETHRACE_FIX_BUGS
