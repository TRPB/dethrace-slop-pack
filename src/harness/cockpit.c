#include "harness/cockpit.h"

#include "harness/hooks.h"
#include "harness/trace.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define COCKPIT_FILE "COCKPIT.TXT"
#define MAX_ENTRIES 64
#define MAX_NAME 32

typedef struct tCockpit_entry {
    char name[MAX_NAME];
    int left;
    int top;
    int right;
    int bottom;
} tCockpit_entry;

static tCockpit_entry s_entries[MAX_ENTRIES];
static int s_count;
static int s_loaded;

static int cockpit_stricmp(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Read one line. A line is a cockpit image name then four integers, in any
 * mixture of commas and whitespace so the file can be written to look like the
 * car TXT's own mirror line. Anything unparseable is skipped rather than
 * rejected: a malformed entry should cost that one cockpit its override, not
 * stop the game loading. */
static void cockpit_parse_line(char* line) {
    char name[MAX_NAME];
    int l, t, r, b;
    char* hash;

    hash = strchr(line, '#');
    if (hash != NULL) {
        *hash = '\0';
    }
    hash = strstr(line, "//");
    if (hash != NULL) {
        *hash = '\0';
    }

    if (sscanf(line, "%31s %d , %d , %d , %d", name, &l, &t, &r, &b) != 5) {
        /* also accept whitespace-separated numbers */
        if (sscanf(line, "%31s %d %d %d %d", name, &l, &t, &r, &b) != 5) {
            return;
        }
    }
    if (r <= l || b <= t) {
        LOG_WARN2("COCKPIT.TXT: ignoring empty rect for %s", name);
        return;
    }
    if (s_count >= MAX_ENTRIES) {
        return;
    }

    /* name was read with "%31s" into a MAX_NAME buffer, so it is always
     * terminated and always fits. snprintf rather than strncpy because GCC
     * cannot see that and warns about a truncation that cannot happen; the
     * explicit terminator covers MSVC's _snprintf, which does not add one. */
    snprintf(s_entries[s_count].name, sizeof(s_entries[s_count].name), "%s", name);
    s_entries[s_count].name[MAX_NAME - 1] = '\0';
    s_entries[s_count].left = l;
    s_entries[s_count].top = t;
    s_entries[s_count].right = r;
    s_entries[s_count].bottom = b;
    s_count++;
}

static void cockpit_load(const char* pData_dir) {
    char path[512];
    char line[256];
    FILE* f;

    s_loaded = 1;
    s_count = 0;
    if (pData_dir == NULL || pData_dir[0] == '\0') {
        return;
    }

    snprintf(path, sizeof(path), "%s%s%s", pData_dir,
        (pData_dir[strlen(pData_dir) - 1] == '/' ? "" : "/"), COCKPIT_FILE);

    /* through the harness hook, so the meld overlay and CUE/ISO installs
     * resolve it the same way as any other data file */
    f = Harness_Hook_fopen(path, "rt");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        cockpit_parse_line(line);
    }
    fclose(f);
    LOG_INFO2("COCKPIT.TXT: %d cockpit override(s)", s_count);
}

int Cockpit_MirrorRect(const char* pData_dir, const char* pPix_name,
    int* pLeft, int* pTop, int* pRight, int* pBottom) {
    int i;

    if (!s_loaded) {
        cockpit_load(pData_dir);
    }
    if (pPix_name == NULL) {
        return 0;
    }
    for (i = 0; i < s_count; i++) {
        if (cockpit_stricmp(s_entries[i].name, pPix_name) == 0) {
            *pLeft = s_entries[i].left;
            *pTop = s_entries[i].top;
            *pRight = s_entries[i].right;
            *pBottom = s_entries[i].bottom;
            return 1;
        }
    }
    return 0;
}
