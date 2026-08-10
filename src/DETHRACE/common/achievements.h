#ifndef ACHIEVEMENTS_H
#define ACHIEVEMENTS_H

#if defined(DETHRACE_FIX_BUGS)

#include "dr_types.h"

// Added by dethrace — trigger types for achievement grouping and mug shot display
typedef enum {
    eAchTrigger_none = 0,
    eAchTrigger_opponent_wasted,
    eAchTrigger_ped_killed,
    eAchTrigger_race_won,
    eAchTrigger_powerup,
} tAchievement_trigger;

typedef struct tAchievement {
    const char* name;
    const char* description;
    int has_progress;
    int progress_pct; // fake 0-100 for prototype
    tAchievement_trigger trigger;
    const char* trigger_param; // opponent name for eAchTrigger_opponent_wasted; NULL for generic
    const char* image_file;    // PIX or FLI filename for display image; NULL = no image
} tAchievement;

// Added by dethrace — achievement index constants
#define ACHIEVEMENT_RACER                    0
#define ACHIEVEMENT_WINNER_WINNER            1
#define ACHIEVEMENT_COP_KILLER               2
#define ACHIEVEMENT_HIT_N_RUN                3
#define ACHIEVEMENT_MASSACRE                 4
#define ACHIEVEMENT_IM_COMING_TO_GET_YOU     5
#define ACHIEVEMENT_PEDICIDE                 6
#define ACHIEVEMENT_I_WAS_IN_THE_WAR         7
#define ACHIEVEMENT_MOST_WANTED              8
#define ACHIEVEMENT_PINBALL_WIZARD           9
#define ACHIEVEMENT_PACIFIST                 10
#define ACHIEVEMENT_RACIST                   11
#define ACHIEVEMENT_PATIENT_ZERO             12
#define ACHIEVEMENT_SPEED_BUMP               13
#define ACHIEVEMENT_ROAD_PIZZA               14
#define ACHIEVEMENT_LAWN_MOWER               15
#define ACHIEVEMENT_TURKEY                   16
#define ACHIEVEMENT_RAMPAGE                  17
#define ACHIEVEMENT_GOING_POSTAL             18
#define ACHIEVEMENT_HUNTER                   19
#define ACHIEVEMENT_PUBLIC_HEALTH_CRISIS     20
#define ACHIEVEMENT_EXTINCTION_EVENT         21
#define ACHIEVEMENT_ENVIRONMENTAL_HAZARD     22
#define ACHIEVEMENT_BOVINE_INTERVENTION      23
#define ACHIEVEMENT_WHOLE_HERD               24
#define ACHIEVEMENT_GRIM_REAPER              25
#define ACHIEVEMENT_NOT_MUCH_OF_A_SPORTS_FAN 26
#define ACHIEVEMENT_FIRST_BLOOD              27
#define ACHIEVEMENT_DEMOLITION_DERBY         28
#define ACHIEVEMENT_ROAD_RAGE                29
#define ACHIEVEMENT_TOTAL_WIPEOUT            30
#define ACHIEVEMENT_SCRAP_DEALER             31
#define ACHIEVEMENT_SERIAL_KILLER            32
#define ACHIEVEMENT_NULL_AND_VOID            33
#define ACHIEVEMENT_MY_CAR_NOW               34
#define ACHIEVEMENT_COLLECTOR                35
#define ACHIEVEMENT_TEST_DRIVE               36
#define ACHIEVEMENT_FREQUENT_FLYER           37
#define ACHIEVEMENT_HIGH_FLYER               38
#define ACHIEVEMENT_CLOSE_SHAVE              39
#define ACHIEVEMENT_SKID_MARK                40
#define ACHIEVEMENT_INSURANCE_NIGHTMARE      41
#define ACHIEVEMENT_NOT_MINE                 42
#define ACHIEVEMENT_CREDIT_WHERE_ITS_DUE     43
#define ACHIEVEMENT_MILLIONAIRE              44
#define ACHIEVEMENT_POWER_HUNGRY             45
#define ACHIEVEMENT_ZAP                      46

extern int gCurrent_achievement_index;

int AchCountUnlocked(void);
void Achievements_Load(int slot);
void Achievements_Save(int slot);
void Achievement_OnPedKilled(int is_footballer);
void Achievement_OnOpponentWasted(const char* pOpponent_name, int opponent_idx);
void Achievement_OnRaceResult(tRace_over_reason reason);
void Achievement_OnRaceStart(void);
void Achievement_OnCreditsEarned(tU32 amount);
void Achievement_OnPlayerPedKilled(float speed_mph, int is_cow, int is_billiards, void* billiards_actor, int is_footballer, int is_proximity_rayed);
void Achievement_OnProxRayStart(tCar_spec* pCar);
void Achievement_OnProxRayEnd(tCar_spec* pCar);
void Achievement_OnAllRacesDone(int game_completed);
void Achievement_OnRepairSpent(void);
void Achievement_OnPowerupCollected(tCar_spec* pCar, int powerup_index);
void Achievement_OnFallenOffWorld(void);
void Achievement_OnCarStolen(void);
void Achievement_OnMineHit(tCar_spec* pCar);
void Achievement_OnPlayerCarFrame(tCar_spec* pCar);
void DoViewAchievements(void);
void Achievement_TriggerPopup(int idx);
void Achievement_TriggerRandomPopup(void);
void Achievement_DrawPopup(void);

#endif // DETHRACE_FIX_BUGS

#endif // ACHIEVEMENTS_H
