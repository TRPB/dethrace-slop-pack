#ifndef HARNESS_CUE_H
#define HARNESS_CUE_H

#include "harness/iso.h"

#include <stddef.h>
#include <stdio.h>

typedef struct {
    int      track_no;  /* as declared in the cue sheet (2, 3, 4, ...) */
    uint32_t start_lba;
    uint32_t end_lba;   /* exclusive: next track's start, or file-derived for the last track */
} tCue_audio_track;

typedef struct tCue_sheet tCue_sheet;

/* Detect and open `path`:
   - a .cue file: parse it (single-FILE layout only -- the common case for a
     Carmageddon rip) and open the .bin it references as an ISO image over
     the data track (track 1, which always sits at byte offset 0 in a
     single-FILE cue sheet -- identical layout to a bare ISO image, so
     iso.c's existing reader needs no changes). *out_cue receives the parsed
     sheet (for audio-track access via the functions below).
   - any other regular file: falls back to Iso_OpenIfFile (bare ISO/BIN
     image); *out_cue is set to NULL.
   - a directory, missing file, or unparseable/unsupported cue sheet (e.g.
     multi-FILE): both the return value and *out_cue are NULL.
   Returns the tIso_image* for DATA reads either way (NULL on failure),
   matching Iso_OpenIfFile's contract so callers don't need to branch
   further to read game assets. */
tIso_image* Cue_OpenDisc(const char* path, tCue_sheet** out_cue);
void        Cue_CloseSheet(tCue_sheet* sheet);

/* Audio track enumeration (track 1 is always the data track and is never
   included here). idx is 0-based over audio tracks only. */
int Cue_AudioTrackCount(tCue_sheet* sheet);
int Cue_GetAudioTrack(tCue_sheet* sheet, int idx, tCue_audio_track* out);

/* Read up to buf_size bytes from the start of an audio track's raw PCM
   (16-bit signed stereo, 44100 Hz, interleaved -- Red Book format, no
   container). Returns the number of bytes actually read. */
size_t Cue_ReadAudioTrackHead(tCue_sheet* sheet, const tCue_audio_track* track, void* buf, size_t buf_size);

/* Write an audio track out as a standalone WAV file (44-byte header + the
   track's raw PCM, streamed in chunks) at out_path, overwriting any existing
   file there. Returns 1 on success, 0 on failure. */
int Cue_WriteAudioTrackWav(tCue_sheet* sheet, const tCue_audio_track* track, const char* out_path);

/* --- Single active-disc convenience layer for non-meld (Meld=0) mode,
       mirroring iso.c's Iso_TrySetupSingle/Iso_FopenSingle/Iso_AccessSingle,
       but disc-format-aware (bare image or .cue sheet). --- */

/* If path is a .cue sheet or a bare ISO/BIN image, set it up as the active
   single disc and return 1. Otherwise return 0 and do nothing. */
int Cue_TrySetupSingle(const char* path);

/* Read-only fopen against the single active disc's data track. Returns NULL
   if no disc is active, mode requests writing, or the path isn't found. */
FILE* Cue_FopenSingle(const char* pathname, const char* mode);

/* access(pathname, F_OK)-equivalent against the single active disc, falling
   back to a real filesystem check first (on-disk override wins). Returns 0
   if found, -1 otherwise. */
int Cue_AccessSingle(const char* pathname);

/* The parsed cue sheet for the single active disc (for CDA audio), or NULL
   if the active disc is a bare image or no disc is active. */
tCue_sheet* Cue_GetSingleSheet(void);

/* Directory listing against the single active disc, single-cursor (like
   OS_GetFirstFileInDirectory/OS_GetNextFileInDirectory). path may be an
   absolute or gApplication_path-prefixed filesystem path -- it's normalised
   the same way Cue_FopenSingle normalises its argument. Returns NULL (and
   ends the cursor) if no disc is active or the directory isn't found. */
const char* Cue_GetFirstFileInDirectorySingle(const char* path);
const char* Cue_GetNextFileInDirectorySingle(void);

#endif /* HARNESS_CUE_H */
