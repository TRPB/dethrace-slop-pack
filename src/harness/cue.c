// CUE/BIN VFS: parses a .cue sheet -- either the common single-FILE layout
// (one .bin holding the ISO9660 data track followed by the Red Book audio
// tracks) or a multi-FILE layout (one .bin per track, as produced by some
// rippers) -- and serves the data track through iso.c's existing sector
// reader, plus raw-PCM extraction for the audio tracks.

#include "harness/cue.h"
#include "harness/iso.h"

/* No <stdint.h> here: it is C99 and MSVC 4.2 has no such header. harness/iso.h
 * above already supplies the fixed-width types portably, typedef'd by hand for
 * that compiler and included from <stdint.h> everywhere else. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && _MSC_VER <= 1020
/* snprintf is C99; this compiler has only the unsafe printf family, so give it
 * the same vsprintf-backed stand-in iso.c uses. Every call here writes into a
 * CUE_MAX_PATH buffer from paths already bounded by the same limit. */
#include <stdarg.h>
static int cue_snprintf(char* buf, int count, const char* fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    (void)count;
    return ret;
}
#define snprintf cue_snprintf
#endif

#ifdef _WIN32
#include <io.h>
#define cue_access(p) _access((p), 0)
#else
#include <unistd.h>
#define cue_access(p) access((p), F_OK)
#endif

#define CUE_MAX_PATH    1024
#define CUE_MAX_TRACKS  32
#define CUE_SECTOR_BYTES 2352
#define CUE_PCM_RATE    44100
#define CUE_PCM_CHANNELS 2
#define CUE_PCM_BITS    16

struct tCue_sheet {
    char bin_path[CUE_MAX_PATH]; /* track 1's (data track's) .bin path */
    FILE* files[CUE_MAX_TRACKS]; /* one handle per FILE statement in the sheet */
    char file_paths[CUE_MAX_TRACKS][CUE_MAX_PATH];
    int file_count;
    int track_file_idx[CUE_MAX_TRACKS]; /* tracks[i] is backed by files[track_file_idx[i]] */
    tCue_audio_track tracks[CUE_MAX_TRACKS];
    int track_count;
};

static uint32_t cue_msf_to_lba(int mm, int ss, int ff) {
    return (uint32_t)((mm * 60 + ss) * 75 + ff);
}

static const char* cue_skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

static int cue_token_eq(const char* p, const char* token) {
    size_t len = strlen(token);
    size_t i;
    for (i = 0; i < len; i++) {
        if (p[i] == '\0') {
            return 0;
        }
        if (toupper((unsigned char)p[i]) != toupper((unsigned char)token[i])) {
            return 0;
        }
    }
    return 1;
}

/* Directory portion of path, including the trailing separator; empty string
   if path has no directory component. */
static void cue_dirname(const char* path, char* out, size_t out_len) {
    const char* last_sep = NULL;
    const char* p;
    size_t len;
    for (p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            last_sep = p;
        }
    }
    if (last_sep == NULL) {
        out[0] = '\0';
        return;
    }
    len = (size_t)(last_sep - path) + 1;
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, path, len);
    out[len] = '\0';
}

/* First double-quoted substring on the line, e.g. FILE "Carm.bin" BINARY. */
static int cue_extract_quoted(const char* line, char* out, size_t out_len) {
    const char* start = strchr(line, '"');
    const char* end;
    size_t len;
    if (start == NULL) {
        return 0;
    }
    start++;
    end = strchr(start, '"');
    if (end == NULL) {
        return 0;
    }
    len = (size_t)(end - start);
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 1;
}

static void cue_free_sheet(tCue_sheet* sheet) {
    int i;
    if (sheet == NULL) {
        return;
    }
    for (i = 0; i < sheet->file_count; i++) {
        if (sheet->files[i] != NULL) {
            fclose(sheet->files[i]);
        }
    }
    free(sheet);
}

// The FILE* backing a given audio track. track may be a copy (callers often
// keep tCue_audio_track by value), so this matches by track_no against the
// sheet's own tracks[]/track_file_idx[] rather than by pointer identity.
static FILE* cue_track_file(tCue_sheet* sheet, const tCue_audio_track* track) {
    int i;
    for (i = 0; i < sheet->track_count; i++) {
        if (sheet->tracks[i].track_no == track->track_no) {
            return sheet->files[sheet->track_file_idx[i]];
        }
    }
    return NULL;
}

// Opens (if not already open) the file named by a FILE statement and
// appends it to sheet->files[], returning its index, or -1 on failure.
// Reused verbatim on every FILE line -- multi-FILE sheets reference a fresh
// file per statement, so no de-duplication is attempted.
static int cue_open_file(tCue_sheet* sheet, const char* dir, const char* bin_name) {
    char path[CUE_MAX_PATH];
    FILE* f;
    int idx;

    if (sheet->file_count >= CUE_MAX_TRACKS) {
        return -1;
    }
    if (dir[0] != '\0') {
        snprintf(path, sizeof(path), "%s%s", dir, bin_name);
    } else {
        strncpy(path, bin_name, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    idx = sheet->file_count;
    sheet->files[idx] = f;
    /* snprintf, not strncpy: source and destination are both CUE_MAX_PATH, so
     * gcc sees strncpy(dst, src, sizeof(dst) - 1) as a copy that may drop the
     * terminator and rejects it under -Wstringop-truncation. */
    snprintf(sheet->file_paths[idx], sizeof(sheet->file_paths[idx]), "%s", path);
    sheet->file_count++;
    return idx;
}

// Parses cue_path -- single-FILE or multi-FILE layout -- and opens every
// .bin it references. Returns NULL on any parse failure or unsupported
// layout (track 1 not MODE1/2352 at LBA 0, missing/unreadable .bin, etc).
static tCue_sheet* cue_parse_and_open(const char* cue_path) {
    FILE* f;
    char line[512];
    char dir[CUE_MAX_PATH];
    char bin_name[CUE_MAX_PATH];
    int cur_file_idx = -1;
    int track1_file_idx = -1;
    int cur_track = -1;
    int cur_track_is_audio = 0;
    int have_track1_data = 0;
    int i;
    tCue_sheet* sheet;

    f = fopen(cue_path, "r");
    if (f == NULL) {
        return NULL;
    }

    sheet = (tCue_sheet*)calloc(1, sizeof(tCue_sheet));
    if (sheet == NULL) {
        fclose(f);
        return NULL;
    }

    cue_dirname(cue_path, dir, sizeof(dir));

    while (fgets(line, sizeof(line), f) != NULL) {
        const char* p = cue_skip_ws(line);

        if (cue_token_eq(p, "FILE")) {
            if (!cue_extract_quoted(p, bin_name, sizeof(bin_name))) {
                fclose(f);
                cue_free_sheet(sheet);
                return NULL;
            }
            cur_file_idx = cue_open_file(sheet, dir, bin_name);
            if (cur_file_idx < 0) {
                fclose(f);
                cue_free_sheet(sheet);
                return NULL;
            }
        } else if (cue_token_eq(p, "TRACK")) {
            const char* rest = cue_skip_ws(p + 5);
            cur_track = atoi(rest);
            while (*rest && *rest != ' ' && *rest != '\t' && *rest != '\r' && *rest != '\n') {
                rest++;
            }
            rest = cue_skip_ws(rest);
            cur_track_is_audio = cue_token_eq(rest, "AUDIO");
            if (cur_track == 1) {
                have_track1_data = cue_token_eq(rest, "MODE1/2352");
                track1_file_idx = cur_file_idx;
            }
        } else if (cue_token_eq(p, "INDEX")) {
            const char* rest = cue_skip_ws(p + 5);
            int index_no = atoi(rest);
            while (*rest && *rest != ' ' && *rest != '\t') {
                rest++;
            }
            rest = cue_skip_ws(rest);
            if (index_no == 1 && cur_track >= 1) {
                int mm, ss, ff;
                if (sscanf(rest, "%d:%d:%d", &mm, &ss, &ff) == 3) {
                    uint32_t lba = cue_msf_to_lba(mm, ss, ff);
                    if (cur_track == 1) {
                        if (lba != 0) {
                            // Track 1 must start at the beginning of its
                            // .bin for iso.c's reader to apply unmodified.
                            fclose(f);
                            cue_free_sheet(sheet);
                            return NULL;
                        }
                    } else if (cur_track_is_audio && sheet->track_count < CUE_MAX_TRACKS) {
                        sheet->tracks[sheet->track_count].track_no = cur_track;
                        sheet->tracks[sheet->track_count].start_lba = lba;
                        sheet->track_file_idx[sheet->track_count] = cur_file_idx;
                        sheet->track_count++;
                    }
                }
            }
        }
    }
    fclose(f);

    if (track1_file_idx < 0 || !have_track1_data) {
        cue_free_sheet(sheet);
        return NULL;
    }
    strncpy(sheet->bin_path, sheet->file_paths[track1_file_idx], sizeof(sheet->bin_path) - 1);
    sheet->bin_path[sizeof(sheet->bin_path) - 1] = '\0';

    // Fill in each audio track's exclusive end_lba: the next track's start
    // if it shares the same .bin (single-FILE layout packs tracks back to
    // back), or its own file's end (in whole sectors) otherwise.
    for (i = 0; i < sheet->track_count; i++) {
        int idx = sheet->track_file_idx[i];
        int shares_next = (i + 1 < sheet->track_count) && (sheet->track_file_idx[i + 1] == idx);
        if (shares_next) {
            sheet->tracks[i].end_lba = sheet->tracks[i + 1].start_lba;
        } else {
            long size;
            fseek(sheet->files[idx], 0, SEEK_END);
            size = ftell(sheet->files[idx]);
            sheet->tracks[i].end_lba = size > 0 ? (uint32_t)(size / CUE_SECTOR_BYTES) : sheet->tracks[i].start_lba;
        }
    }

    return sheet;
}

tIso_image* Cue_OpenDisc(const char* path, tCue_sheet** out_cue) {
    size_t len;

    *out_cue = NULL;
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }

    len = strlen(path);
    if (len >= 4 && cue_token_eq(path + len - 4, ".cue")) {
        tCue_sheet* sheet = cue_parse_and_open(path);
        tIso_image* img;
        if (sheet == NULL) {
            return NULL;
        }
        // Track 1 sits at byte offset 0 of its .bin, identical to a bare
        // ISO image, so the existing reader applies unmodified. Open a
        // second, independent handle to that .bin rather than sharing
        // sheet->files[]: tIso_image and tCue_sheet each manage their own
        // FILE* lifetime, and iso.c's sector reads are not synchronised
        // with cue.c's audio-track reads.
        img = Iso_Open(sheet->bin_path);
        if (img == NULL) {
            cue_free_sheet(sheet);
            return NULL;
        }
        *out_cue = sheet;
        return img;
    }

    return Iso_OpenIfFile(path);
}

void Cue_CloseSheet(tCue_sheet* sheet) {
    cue_free_sheet(sheet);
}

int Cue_AudioTrackCount(tCue_sheet* sheet) {
    return sheet != NULL ? sheet->track_count : 0;
}

int Cue_GetAudioTrack(tCue_sheet* sheet, int idx, tCue_audio_track* out) {
    if (sheet == NULL || idx < 0 || idx >= sheet->track_count) {
        return 0;
    }
    *out = sheet->tracks[idx];
    return 1;
}

size_t Cue_ReadAudioTrackHead(tCue_sheet* sheet, const tCue_audio_track* track, void* buf, size_t buf_size) {
    FILE* bin_f;
    long offset;
    size_t max_bytes;

    if (sheet == NULL || track == NULL) {
        return 0;
    }
    bin_f = cue_track_file(sheet, track);
    if (bin_f == NULL) {
        return 0;
    }
    offset = (long)track->start_lba * CUE_SECTOR_BYTES;
    max_bytes = (size_t)(track->end_lba - track->start_lba) * CUE_SECTOR_BYTES;
    if (buf_size > max_bytes) {
        buf_size = max_bytes;
    }
    if (fseek(bin_f, offset, SEEK_SET) != 0) {
        return 0;
    }
    return fread(buf, 1, buf_size, bin_f);
}

// 44-byte canonical PCM WAV header for CUE_PCM_CHANNELS/CUE_PCM_RATE/
// CUE_PCM_BITS audio of the given byte length.
static void cue_write_wav_header(FILE* out, uint32_t pcm_bytes) {
    uint16_t block_align = CUE_PCM_CHANNELS * (CUE_PCM_BITS / 8);
    uint32_t byte_rate = CUE_PCM_RATE * block_align;
    uint32_t riff_size = 36 + pcm_bytes;
    uint16_t fmt_size = 16;
    uint16_t audio_format = 1; /* PCM */
    uint16_t channels = CUE_PCM_CHANNELS;
    uint32_t sample_rate = CUE_PCM_RATE;
    uint16_t bits_per_sample = CUE_PCM_BITS;

    fwrite("RIFF", 1, 4, out);
    fwrite(&riff_size, 4, 1, out);
    fwrite("WAVE", 1, 4, out);
    fwrite("fmt ", 1, 4, out);
    fwrite(&fmt_size, 4, 1, out);
    fwrite(&audio_format, 2, 1, out);
    fwrite(&channels, 2, 1, out);
    fwrite(&sample_rate, 4, 1, out);
    fwrite(&byte_rate, 4, 1, out);
    fwrite(&block_align, 2, 1, out);
    fwrite(&bits_per_sample, 2, 1, out);
    fwrite("data", 1, 4, out);
    fwrite(&pcm_bytes, 4, 1, out);
}

int Cue_WriteAudioTrackWav(tCue_sheet* sheet, const tCue_audio_track* track, const char* out_path) {
    FILE* bin_f;
    FILE* out;
    long offset;
    uint32_t remaining;
    unsigned char buf[65536];
    int ok = 1;

    if (sheet == NULL || track == NULL) {
        return 0;
    }
    if (track->end_lba <= track->start_lba) {
        return 0;
    }
    bin_f = cue_track_file(sheet, track);
    if (bin_f == NULL) {
        return 0;
    }

    out = fopen(out_path, "wb");
    if (out == NULL) {
        return 0;
    }

    remaining = (track->end_lba - track->start_lba) * CUE_SECTOR_BYTES;
    cue_write_wav_header(out, remaining);

    offset = (long)track->start_lba * CUE_SECTOR_BYTES;
    if (fseek(bin_f, offset, SEEK_SET) != 0) {
        fclose(out);
        return 0;
    }
    while (remaining > 0 && ok) {
        size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);
        size_t n = fread(buf, 1, chunk, bin_f);
        if (n != chunk || fwrite(buf, 1, n, out) != n) {
            ok = 0;
            break;
        }
        remaining -= (uint32_t)n;
    }

    fclose(out);
    return ok;
}

// ---------------------------------------------------------------------------
// Single active-disc convenience layer (non-meld mode)
// ---------------------------------------------------------------------------

static tIso_image* s_single_img = NULL;
static tCue_sheet* s_single_cue = NULL;

int Cue_TrySetupSingle(const char* path) {
    tCue_sheet* cue = NULL;
    tIso_image* img = Cue_OpenDisc(path, &cue);
    if (img == NULL) {
        return 0;
    }
    if (s_single_img != NULL) {
        Iso_Close(s_single_img);
    }
    if (s_single_cue != NULL) {
        cue_free_sheet(s_single_cue);
    }
    s_single_img = img;
    s_single_cue = cue;
    return 1;
}

// Callers that already pass an ISO-relative path (e.g. harness.c's
// "DATA/RACES/CASTLE.TXT" probes) are unaffected -- the "DATA" segment is
// found right at the start. Callers that forward a DRfopen() path built from
// gApplication_path (an absolute "<cwd>/DATA/..." path, since a CUE/ISO
// single-disc game is never chdir'd into) get the ISO-root-relative tail
// instead, which is what Iso_FindFile expects.
static const char* cue_relative_tail(const char* pathname) {
    static const char* candidates[] = { "DATA", "data", "MUSIC", "music" };
    const char* best = NULL;
    size_t i;
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        const char* found = strstr(pathname, candidates[i]);
        if (found != NULL && (found == pathname || found[-1] == '/' || found[-1] == '\\')) {
            if (best == NULL || found > best) {
                best = found;
            }
        }
    }
    return best != NULL ? best : pathname;
}

FILE* Cue_FopenSingle(const char* pathname, const char* mode) {
    if (s_single_img == NULL) {
        return NULL;
    }
    if (strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL || strchr(mode, '+') != NULL) {
        return NULL;
    }
    return Iso_Fopen(s_single_img, cue_relative_tail(pathname));
}

int Cue_AccessSingle(const char* pathname) {
    tIso_entry entry;
    if (cue_access(pathname) == 0) {
        return 0;
    }
    if (s_single_img != NULL && Iso_FindFile(s_single_img, pathname, &entry)) {
        return 0;
    }
    return -1;
}

// Single-cursor directory listing over the active image, mirroring
// OS_GetFirstFileInDirectory/OS_GetNextFileInDirectory's semantics (see
// harness.c's Harness_Hook_GetFirstFileInDirectory, which falls back to this
// when the real filesystem has no such directory -- e.g. LoadInRegisteeDir's
// DATA/REG/PALETTES etc, which a bare CUE/ISO install never has on disk).
static tIso_entry s_single_dir_entries[4096];
static int s_single_dir_count = 0;
static int s_single_dir_pos = 0;

const char* Cue_GetFirstFileInDirectorySingle(const char* path) {
    if (s_single_img == NULL) {
        return NULL;
    }
    if (Iso_ListDir(s_single_img, cue_relative_tail(path), s_single_dir_entries,
            (int)(sizeof(s_single_dir_entries) / sizeof(s_single_dir_entries[0])),
            &s_single_dir_count) != 0) {
        s_single_dir_count = 0;
        return NULL;
    }
    s_single_dir_pos = 0;
    return Cue_GetNextFileInDirectorySingle();
}

const char* Cue_GetNextFileInDirectorySingle(void) {
    if (s_single_dir_pos >= s_single_dir_count) {
        return NULL;
    }
    return s_single_dir_entries[s_single_dir_pos++].name;
}

tCue_sheet* Cue_GetSingleSheet(void) {
    return s_single_cue;
}
