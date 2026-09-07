#include "harness/iso.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && _MSC_VER <= 1020
#include <stdarg.h>
static int iso_snprintf(char* buf, int count, const char* fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    (void)count;
    return ret;
}
#define snprintf iso_snprintf
#endif

/* strtok_r is POSIX and MSVC has never provided it at any version - only
 * strtok_s, and not even that before VS2005. Without a definition it is
 * implicitly declared as returning int (C4047 on the assignment to char*) and
 * then fails to link (LNK2019, _strtok_r unresolved). Applies to every MSVC,
 * not just the ancient one used for the reccmp build. */
#if defined(_MSC_VER)
static char* iso_strtok_r(char* str, const char* delim, char** saveptr) {
    char* start;

    if (str == NULL) {
        str = *saveptr;
    }
    if (str == NULL) {
        return NULL;
    }
    str += strspn(str, delim);
    if (*str == '\0') {
        *saveptr = NULL;
        return NULL;
    }
    start = str;
    str = strpbrk(start, delim);
    if (str == NULL) {
        *saveptr = NULL;
    } else {
        *str = '\0';
        *saveptr = str + 1;
    }
    return start;
}
#define strtok_r iso_strtok_r
#endif

/* Raw Mode-1 sector layout: 12 sync + 4 header + 2048 data + 288 ECC */
#define ISO_SECTOR_RAW  2352
#define ISO_SECTOR_SKIP 16
#define ISO_SECTOR_DATA 2048

#define ISO_PVD_SECTOR            16
#define ISO_PVD_ROOT_RECORD_OFFS  156

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
/* struct _stat, _stat() and the S_IF* macros. Newer toolchains drag these in
 * via the headers above, but MSVC 4.2 does not, so Iso_OpenIfFile fails to
 * compile without them (C2079 on struct _stat, C2065 on S_IFMT/S_IFREG).
 * sys/types.h has to come first, hence the formatting exemption -- sorting
 * these alphabetically breaks the old compiler. */
/* clang-format off */
#include <sys/types.h>
#include <sys/stat.h>
/* clang-format on */
#define iso_access(p) _access((p), 0)
#define iso_mkdir(p)  _mkdir(p)
#define iso_stat_t    struct _stat
#define iso_stat(p, s) _stat((p), (s))
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define iso_access(p) access((p), F_OK)
#define iso_mkdir(p)  mkdir((p), 0755)
#define iso_stat_t    struct stat
#define iso_stat(p, s) stat((p), (s))
#endif

struct tIso_image {
    FILE* f;
};

static int iso_read_sector(FILE* f, uint32_t lba, uint8_t* buf) {
    long offset = (long)lba * ISO_SECTOR_RAW + ISO_SECTOR_SKIP;
    if (fseek(f, offset, SEEK_SET) != 0) {
        return 0;
    }
    return (int)fread(buf, 1, ISO_SECTOR_DATA, f) == ISO_SECTOR_DATA;
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int iso_name_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Find one path component named 'target' inside the directory at dir_lba.
   Sets *out_lba, *out_size, *out_is_dir on success. Returns 1 if found. */
static int iso_find_entry(FILE* f, uint32_t dir_lba, uint32_t dir_size,
                          const char* target, int want_dir,
                          uint32_t* out_lba, uint32_t* out_size, int* out_is_dir) {
    uint32_t sectors = (dir_size + ISO_SECTOR_DATA - 1) / ISO_SECTOR_DATA;
    uint32_t s;
    uint8_t buf[ISO_SECTOR_DATA];

    for (s = 0; s < sectors; s++) {
        int pos = 0;
        if (!iso_read_sector(f, dir_lba + s, buf)) {
            return 0;
        }
        while (pos < ISO_SECTOR_DATA) {
            uint8_t rec_len = buf[pos];
            if (rec_len == 0) {
                break;
            }
            {
                int is_dir = (buf[pos + 25] & 0x02) != 0;
                uint8_t id_len = buf[pos + 32];
                char id[33];

                if (id_len > 0 && id_len <= 32) {
                    char* semi;
                    memcpy(id, &buf[pos + 33], id_len);
                    id[id_len] = '\0';
                    semi = strchr(id, ';');
                    if (semi) {
                        *semi = '\0';
                    }
                    if ((want_dir < 0 || is_dir == want_dir) && iso_name_eq(id, target)) {
                        *out_lba = le32(&buf[pos + 2]);
                        *out_size = le32(&buf[pos + 10]);
                        *out_is_dir = is_dir;
                        return 1;
                    }
                }
            }
            pos += rec_len;
        }
    }
    return 0;
}

/* Navigate every slash-separated component of path_copy (destructively
   tokenized) from the root, writing the resolved directory's LBA and size to
   *lba and *size. Returns 1 on success, 0 if any component is missing or not
   a directory. An empty path_copy resolves to the root directory. */
static int iso_resolve_dir(FILE* f, char* path_copy, uint32_t* lba, uint32_t* size) {
    uint8_t pvd[ISO_SECTOR_DATA];
    int is_dir;
    char* component;
    char* strtok_state;

    if (!iso_read_sector(f, ISO_PVD_SECTOR, pvd)) {
        return 0;
    }
    *lba = le32(&pvd[ISO_PVD_ROOT_RECORD_OFFS + 2]);
    *size = le32(&pvd[ISO_PVD_ROOT_RECORD_OFFS + 10]);

    // strtok_r, not strtok: this can run while a caller further up the stack
    // (e.g. LoadSpeedo mid-parse of a car's DATA/CARS/*.TXT line) has its own
    // strtok() sequence in progress. LoadPixelmap() calls made while walking
    // that line trigger a DRfopen() that lands here -- plain strtok's shared
    // state would stomp on the outer scan and hand it a stale/NULL token.
    component = strtok_r(path_copy, "/\\", &strtok_state);
    while (component != NULL) {
        if (!iso_find_entry(f, *lba, *size, component, 1, lba, size, &is_dir)) {
            return 0;
        }
        component = strtok_r(NULL, "/\\", &strtok_state);
    }
    return 1;
}

tIso_image* Iso_Open(const char* path) {
    FILE* f;
    uint8_t pvd[ISO_SECTOR_DATA];
    tIso_image* img;

    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (!iso_read_sector(f, ISO_PVD_SECTOR, pvd)) {
        fclose(f);
        return NULL;
    }
    if (pvd[0] != 1 || memcmp(&pvd[1], "CD001", 5) != 0) {
        fclose(f);
        return NULL;
    }
    img = (tIso_image*)malloc(sizeof(*img));
    if (img == NULL) {
        fclose(f);
        return NULL;
    }
    img->f = f;
    return img;
}

void Iso_Close(tIso_image* img) {
    if (img == NULL) {
        return;
    }
    fclose(img->f);
    free(img);
}

int Iso_ListDir(tIso_image* img, const char* dir_path,
                tIso_entry* entries, int max_entries, int* count) {
    uint32_t lba, size;
    char path_copy[256];

    *count = 0;
    strncpy(path_copy, dir_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    if (!iso_resolve_dir(img->f, path_copy, &lba, &size)) {
        return -1;
    }

    /* List files in the final directory. */
    {
        uint32_t sectors = (size + ISO_SECTOR_DATA - 1) / ISO_SECTOR_DATA;
        uint32_t s;
        uint8_t buf[ISO_SECTOR_DATA];

        for (s = 0; s < sectors && *count < max_entries; s++) {
            int pos = 0;
            if (!iso_read_sector(img->f, lba + s, buf)) {
                return -1;
            }
            while (pos < ISO_SECTOR_DATA && *count < max_entries) {
                uint8_t rec_len = buf[pos];
                if (rec_len == 0) {
                    break;
                }
                {
                    int entry_is_dir = (buf[pos + 25] & 0x02) != 0;
                    uint8_t id_len = buf[pos + 32];
                    if (!entry_is_dir && id_len > 0 && id_len <= 31) {
                        char id[32];
                        char* semi;
                        int j;
                        memcpy(id, &buf[pos + 33], id_len);
                        id[id_len] = '\0';
                        semi = strchr(id, ';');
                        if (semi) {
                            *semi = '\0';
                        }
                        for (j = 0; id[j]; j++) {
                            id[j] = (char)toupper((unsigned char)id[j]);
                        }
                        memset(&entries[*count], 0, sizeof(entries[*count]));
                        memcpy(entries[*count].name, id, sizeof(entries[*count].name) - 1);
                        entries[*count].lba = le32(&buf[pos + 2]);
                        entries[*count].data_size = le32(&buf[pos + 10]);
                        (*count)++;
                    }
                }
                pos += rec_len;
            }
        }
    }
    return 0;
}

FILE* Iso_ServeEntry(tIso_image* img, const tIso_entry* entry) {
    uint32_t remaining = entry->data_size;
    uint32_t lba = entry->lba;
    uint8_t sector_buf[ISO_SECTOR_DATA];
    FILE* out;

    out = tmpfile();
    if (out == NULL) {
        return NULL;
    }
    while (remaining > 0) {
        uint32_t chunk = remaining < ISO_SECTOR_DATA ? remaining : ISO_SECTOR_DATA;
        if (!iso_read_sector(img->f, lba, sector_buf)) {
            fclose(out);
            return NULL;
        }
        if (fwrite(sector_buf, 1, chunk, out) != chunk) {
            fclose(out);
            return NULL;
        }
        remaining -= chunk;
        lba++;
    }
    rewind(out);
    return out;
}

int Iso_FindGogInDir(const char* dir, char* out, int out_size) {
#ifdef _WIN32
    WIN32_FIND_DATAA find_data;
    HANDLE h;
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.GOG", dir);
    h = FindFirstFileA(pattern, &find_data);
    if (h == INVALID_HANDLE_VALUE) {
        snprintf(pattern, sizeof(pattern), "%s\\*.gog", dir);
        h = FindFirstFileA(pattern, &find_data);
        if (h == INVALID_HANDLE_VALUE) {
            return 0;
        }
    }
    snprintf(out, out_size, "%s\\%s", dir, find_data.cFileName);
    FindClose(h);
    return 1;
#else
    DIR* d = opendir(dir);
    struct dirent* ent;
    if (d == NULL) {
        return 0;
    }
    while ((ent = readdir(d)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        if (nlen >= 5) {
            const char* ext = ent->d_name + nlen - 3; /* points at "GOG", not ".GOG" */
            if (toupper((unsigned char)ext[0]) == 'G' &&
                toupper((unsigned char)ext[1]) == 'O' &&
                toupper((unsigned char)ext[2]) == 'G' &&
                ext[3] == '\0' && ext[-1] == '.') {
                snprintf(out, out_size, "%s/%s", dir, ent->d_name);
                closedir(d);
                return 1;
            }
        }
    }
    closedir(d);
    return 0;
#endif
}

int Iso_FindFile(tIso_image* img, const char* file_path, tIso_entry* out) {
    char dir_copy[256];
    const char* leaf;
    const char* last_slash = NULL;
    const char* p;
    uint32_t lba, size;
    int id_len, j;

    for (p = file_path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            last_slash = p;
        }
    }
    if (last_slash != NULL) {
        size_t dir_len = (size_t)(last_slash - file_path);
        if (dir_len >= sizeof(dir_copy)) {
            dir_len = sizeof(dir_copy) - 1;
        }
        memcpy(dir_copy, file_path, dir_len);
        dir_copy[dir_len] = '\0';
        leaf = last_slash + 1;
    } else {
        dir_copy[0] = '\0';
        leaf = file_path;
    }

    if (!iso_resolve_dir(img->f, dir_copy, &lba, &size)) {
        return 0;
    }
    if (!iso_find_entry(img->f, lba, size, leaf, 0, &out->lba, &out->data_size, &j)) {
        return 0;
    }

    memset(out->name, 0, sizeof(out->name));
    id_len = (int)strlen(leaf);
    if (id_len >= (int)sizeof(out->name)) {
        id_len = (int)sizeof(out->name) - 1;
    }
    for (j = 0; j < id_len; j++) {
        out->name[j] = (char)toupper((unsigned char)leaf[j]);
    }
    return 1;
}

FILE* Iso_Fopen(tIso_image* img, const char* file_path) {
    tIso_entry entry;
    if (!Iso_FindFile(img, file_path, &entry)) {
        return NULL;
    }
    return Iso_ServeEntry(img, &entry);
}

tIso_image* Iso_OpenIfFile(const char* path) {
    iso_stat_t st;
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    if (iso_stat(path, &st) != 0) {
        return NULL;
    }
    if ((st.st_mode & S_IFMT) != S_IFREG) {
        return NULL;
    }
    return Iso_Open(path);
}

// ---------------------------------------------------------------------------
// Single active-image convenience layer (non-meld mode)
// ---------------------------------------------------------------------------

static tIso_image* s_single_img = NULL;

int Iso_TrySetupSingle(const char* path) {
    tIso_image* img = Iso_OpenIfFile(path);
    if (img == NULL) {
        return 0;
    }
    if (s_single_img != NULL) {
        Iso_Close(s_single_img);
    }
    s_single_img = img;
    return 1;
}

FILE* Iso_FopenSingle(const char* pathname, const char* mode) {
    if (s_single_img == NULL) {
        return NULL;
    }
    if (strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL || strchr(mode, '+') != NULL) {
        return NULL;
    }
    return Iso_Fopen(s_single_img, pathname);
}

int Iso_AccessSingle(const char* pathname) {
    tIso_entry entry;
    if (iso_access(pathname) == 0) {
        return 0;
    }
    if (s_single_img != NULL && Iso_FindFile(s_single_img, pathname, &entry)) {
        return 0;
    }
    return -1;
}

void Iso_EnsureWritableDataDirs(void) {
    iso_mkdir("DATA");
    iso_mkdir("DATA/SAVEGAME");
    iso_mkdir("DATA/SAVEGAME_M");
}
