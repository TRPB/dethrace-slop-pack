#include "tests.h"

#include <string.h>

#include "common/globvars.h"
#include "common/loading.h"
#include "common/structur.h"
#include "common/world.h"
#include "dr_types.h"
#include "harness/config.h"
#include "harness/meld.h"

// Set up a minimal gOpponents array for ChooseOpponent tests.
// Opponent layout:
//   index 0 - Max (player, str=-1, car_number=100)
//   index 1 - Anna (other player, str=-1, car_number=101)
//   index 2 - Kutter (regular str=1 opponent, car_number=7)
//   index 3 - Ed 101 (regular str=1 opponent, car_number=10)
//   index 4 - Mech Maniac (regular str=2 opponent, car_number=16)
static tOpponent s_test_opponents[5];
static const int s_test_opponent_count = 5;

static void setup_opponents(void) {
    memset(s_test_opponents, 0, sizeof(s_test_opponents));

    strcpy(s_test_opponents[0].name, "Max Damage");
    s_test_opponents[0].strength_rating = -1;
    s_test_opponents[0].car_number = 100;

    strcpy(s_test_opponents[1].name, "Die Anna");
    s_test_opponents[1].strength_rating = -1;
    s_test_opponents[1].car_number = 101;

    strcpy(s_test_opponents[2].name, "Kutter");
    s_test_opponents[2].strength_rating = 1;
    s_test_opponents[2].car_number = 7;

    strcpy(s_test_opponents[3].name, "Ed 101");
    s_test_opponents[3].strength_rating = 1;
    s_test_opponents[3].car_number = 10;

    strcpy(s_test_opponents[4].name, "Mech Maniac");
    s_test_opponents[4].strength_rating = 2;
    s_test_opponents[4].car_number = 16;

    gOpponents = s_test_opponents;
    gNumber_of_racers = s_test_opponent_count;
}

// ChooseOpponent with add_other_player_as_opponent=0: Anna must never appear.
void test_structur_ChooseOpponent_other_player_disabled(void) {
    int i;
    int had_scum;
    int anna_seen = 0;

    setup_opponents();
    gProgram_state.frank_or_anniness = eFrankie;
    gProgram_state.current_car.index = 0;
    harness_game_config.add_other_player_as_opponent = 0;

    for (i = 0; i < 100; i++) {
        int j;
        for (j = 0; j < s_test_opponent_count; j++) {
            s_test_opponents[j].picked = 0;
        }
        had_scum = 0;
        int chosen = ChooseOpponent(1, &had_scum);
        if (chosen == 1) {
            anna_seen = 1;
            break;
        }
    }

    TEST_ASSERT_EQUAL_INT(0, anna_seen);
}

// ChooseOpponent with add_other_player_as_opponent=1: Anna must appear at least once in 100 tries.
void test_structur_ChooseOpponent_other_player_enabled(void) {
    int i;
    int had_scum;
    int anna_seen = 0;

    setup_opponents();
    gProgram_state.frank_or_anniness = eFrankie;
    gProgram_state.current_car.index = 0;
    harness_game_config.add_other_player_as_opponent = 1;

    for (i = 0; i < 100; i++) {
        int j;
        for (j = 0; j < s_test_opponent_count; j++) {
            s_test_opponents[j].picked = 0;
        }
        had_scum = 0;
        int chosen = ChooseOpponent(1, &had_scum);
        if (chosen == 1) {
            anna_seen = 1;
            break;
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, anna_seen, "Anna never appeared after 100 races");
}

// Playing as Anna with the feature on, Max (car_number=100) must appear.
void test_structur_ChooseOpponent_as_anna_max_appears(void) {
    int i;
    int had_scum;
    int max_seen = 0;

    setup_opponents();
    gProgram_state.frank_or_anniness = eAnnie;
    gProgram_state.current_car.index = 1;
    harness_game_config.add_other_player_as_opponent = 1;

    for (i = 0; i < 100; i++) {
        int j;
        for (j = 0; j < s_test_opponent_count; j++) {
            s_test_opponents[j].picked = 0;
        }
        had_scum = 0;
        int chosen = ChooseOpponent(1, &had_scum);
        if (chosen == 0) {
            max_seen = 1;
            break;
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, max_seen, "Max never appeared after 100 races (playing as Anna)");
}

// With the real merged opponent list (meld active), Anna should still appear
// when add_other_player_as_opponent=1.
void test_structur_ChooseOpponent_other_player_enabled_meld(void) {
    REQUIRES_DATA_DIRECTORY();

    int i;
    int had_scum;
    int anna_seen = 0;
    int anna_car_number = 101;

    LoadOpponents();

    gProgram_state.frank_or_anniness = eFrankie;
    gProgram_state.current_car.index = 0;
    harness_game_config.add_other_player_as_opponent = 1;

    for (i = 0; i < 200; i++) {
        int j;
        for (j = 0; j < gNumber_of_racers; j++) {
            gOpponents[j].picked = 0;
        }
        had_scum = 0;
        int chosen = ChooseOpponent(1, &had_scum);
        if (gOpponents[chosen].car_number == anna_car_number) {
            anna_seen = 1;
            break;
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, anna_seen, "Anna never appeared in 200 races (meld active)");
}

// Anna's car file name in gOpponents should be "NEWANNIE.TXT" when the
// test data directory is CARSPLAT (Splat Pack).
void test_structur_anna_car_file_name(void) {
    REQUIRES_DATA_DIRECTORY();

    LoadOpponents();

    // Anna is at gOpponents[1] (car_number=101, str=-1).
    TEST_ASSERT_EQUAL_INT(101, gOpponents[1].car_number);
    TEST_ASSERT_EQUAL_INT(-1, gOpponents[1].strength_rating);
    TEST_ASSERT_EQUAL_STRING("NEWANNIE.TXT", gOpponents[1].car_file_name);
}

void test_structur_suite(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_structur_ChooseOpponent_other_player_disabled);
    RUN_TEST(test_structur_ChooseOpponent_other_player_enabled);
    RUN_TEST(test_structur_ChooseOpponent_as_anna_max_appears);
    RUN_TEST(test_structur_ChooseOpponent_other_player_enabled_meld);
    RUN_TEST(test_structur_anna_car_file_name);
}
