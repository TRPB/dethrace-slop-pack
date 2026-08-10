#include "tests.h"

#include <string.h>
#include <sys/stat.h>

#include "common/globvars.h"
#include "common/stats.h"
#include "harness/meld.h"

// Points gApplication_path at a temp directory so Stats_Save/Load use it.
// Uses the non-meld SAVEGAME path to keep things simple.
static char s_orig_app_path[256];
static char s_stats_dir[PATH_MAX + 1];

static void stats_setup(void) {
    char tmp[PATH_MAX + 1];
    create_temp_file(tmp, "stats_test");
    // tmp is now a file — repurpose its parent as our fake app path
    // We use gApplication_path directly; SAVEGAME subdir is created by Stats_Save.
    strcpy(s_orig_app_path, gApplication_path);

    // Build a fresh temp dir as the fake DATA root
    create_temp_file(s_stats_dir, "statsroot");
    // Remove the temp file so we can use that path as a directory
    remove(s_stats_dir);
    mkdir(s_stats_dir, 0755);

    strncpy(gApplication_path, s_stats_dir, sizeof(gApplication_path) - 1);
    gApplication_path[sizeof(gApplication_path) - 1] = '\0';
    gMeld_active = 0;
    memset(&gGame_stats, 0, sizeof(gGame_stats));
}

static void stats_teardown(void) {
    strncpy(gApplication_path, s_orig_app_path, sizeof(gApplication_path) - 1);
    gApplication_path[sizeof(gApplication_path) - 1] = '\0';
    gMeld_active = 0;
}

// Save writes a STATS0 file and Load reads it back correctly.
void test_stats_save_and_load_roundtrip(void) {
    stats_setup();

    gGame_stats.total_peds_killed = 42;
    gGame_stats.total_powerups_collected = 7;
    gGame_stats.total_races_won = 3;
    gGame_stats.races_won_by_peds = 1;
    gGame_stats.races_won_by_checkpoints = 1;
    gGame_stats.races_won_by_opponents = 1;
    gGame_stats.total_opponents_wasted = 5;
    gGame_stats.max_peds_single_race = 20;
    gGame_stats.total_repair_spent = 1500;

    Stats_Save(0);
    memset(&gGame_stats, 0, sizeof(gGame_stats));
    Stats_Load(0);

    TEST_ASSERT_EQUAL_UINT32(42, gGame_stats.total_peds_killed);
    TEST_ASSERT_EQUAL_UINT32(7, gGame_stats.total_powerups_collected);
    TEST_ASSERT_EQUAL_UINT32(3, gGame_stats.total_races_won);
    TEST_ASSERT_EQUAL_UINT32(1, gGame_stats.races_won_by_peds);
    TEST_ASSERT_EQUAL_UINT32(1, gGame_stats.races_won_by_checkpoints);
    TEST_ASSERT_EQUAL_UINT32(1, gGame_stats.races_won_by_opponents);
    TEST_ASSERT_EQUAL_UINT32(5, gGame_stats.total_opponents_wasted);
    TEST_ASSERT_EQUAL_UINT32(20, gGame_stats.max_peds_single_race);
    TEST_ASSERT_EQUAL_UINT32(1500, gGame_stats.total_repair_spent);

    stats_teardown();
}

// Stats slots are independent: slot 1 does not affect slot 0.
void test_stats_slots_are_independent(void) {
    stats_setup();

    gGame_stats.total_peds_killed = 10;
    Stats_Save(0);

    gGame_stats.total_peds_killed = 99;
    Stats_Save(1);

    memset(&gGame_stats, 0, sizeof(gGame_stats));
    Stats_Load(0);
    TEST_ASSERT_EQUAL_UINT32(10, gGame_stats.total_peds_killed);

    memset(&gGame_stats, 0, sizeof(gGame_stats));
    Stats_Load(1);
    TEST_ASSERT_EQUAL_UINT32(99, gGame_stats.total_peds_killed);

    stats_teardown();
}

// Overwriting an existing slot with new data replaces it completely.
void test_stats_overwrite(void) {
    stats_setup();

    gGame_stats.total_peds_killed = 10;
    Stats_Save(0);

    gGame_stats.total_peds_killed = 50;
    gGame_stats.total_races_won = 3;
    Stats_Save(0);

    memset(&gGame_stats, 0, sizeof(gGame_stats));
    Stats_Load(0);

    TEST_ASSERT_EQUAL_UINT32(50, gGame_stats.total_peds_killed);
    TEST_ASSERT_EQUAL_UINT32(3, gGame_stats.total_races_won);

    stats_teardown();
}

// Loading a non-existent slot leaves gGame_stats zeroed.
void test_stats_load_missing_file_gives_zero(void) {
    stats_setup();

    gGame_stats.total_peds_killed = 99;
    Stats_Load(0); // no file written

    TEST_ASSERT_EQUAL_UINT32(0, gGame_stats.total_peds_killed);

    stats_teardown();
}

// A file with a corrupted checksum is rejected; stats reset to zero.
void test_stats_load_bad_checksum_gives_zero(void) {
    char path[PATH_MAX + 1];
    FILE* f;
    tGame_stats corrupted;
    stats_setup();

    gGame_stats.total_peds_killed = 42;
    Stats_Save(0);

    // Flip a bit in the file to corrupt the data (not the checksum field)
    sprintf(path, "%s/SAVEGAME/STATS0", s_stats_dir);
    f = fopen(path, "r+b");
    TEST_ASSERT_NOT_NULL(f);
    fread(&corrupted, 1, sizeof(corrupted), f);
    corrupted.total_peds_killed ^= 0xFF; // corrupt data without touching checksum
    rewind(f);
    fwrite(&corrupted, 1, sizeof(corrupted), f);
    fclose(f);

    memset(&gGame_stats, 0xFF, sizeof(gGame_stats));
    Stats_Load(0);

    TEST_ASSERT_EQUAL_UINT32(0, gGame_stats.total_peds_killed);
    TEST_ASSERT_EQUAL_UINT32(STATS_VERSION, gGame_stats.version);

    stats_teardown();
}

// A file with a wrong version is rejected; stats reset to zero.
void test_stats_load_wrong_version_gives_zero(void) {
    char path[PATH_MAX + 1];
    FILE* f;
    tGame_stats bad_version;
    stats_setup();

    gGame_stats.total_peds_killed = 42;
    Stats_Save(0);

    sprintf(path, "%s/SAVEGAME/STATS0", s_stats_dir);
    f = fopen(path, "r+b");
    TEST_ASSERT_NOT_NULL(f);
    fread(&bad_version, 1, sizeof(bad_version), f);
    bad_version.version = 0xDEAD;
    rewind(f);
    fwrite(&bad_version, 1, sizeof(bad_version), f);
    fclose(f);

    memset(&gGame_stats, 0xFF, sizeof(gGame_stats));
    Stats_Load(0);

    TEST_ASSERT_EQUAL_UINT32(0, gGame_stats.total_peds_killed);

    stats_teardown();
}

void test_stats_suite(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_stats_save_and_load_roundtrip);
    RUN_TEST(test_stats_slots_are_independent);
    RUN_TEST(test_stats_overwrite);
    RUN_TEST(test_stats_load_missing_file_gives_zero);
    RUN_TEST(test_stats_load_bad_checksum_gives_zero);
    RUN_TEST(test_stats_load_wrong_version_gives_zero);
}
