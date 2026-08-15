#ifndef HARNESS_ISO_H
#define HARNESS_ISO_H

#if defined(_MSC_VER) && _MSC_VER <= 1020
typedef unsigned char  uint8_t;
typedef unsigned long  uint32_t;
typedef unsigned long  uint64_t;
#else
#include <stdint.h>
#endif
#include <stdio.h>

typedef struct {
    char     name[32];   /* uppercase, no version suffix e.g. "CRASH.SMK" */
    uint32_t lba;
    uint32_t data_size;
} tIso_entry;

typedef struct tIso_image tIso_image;

/* Open a raw Mode-1 ISO 9660 image (2352-byte sectors).
   Returns NULL if the file cannot be opened or is not a valid ISO. */
tIso_image* Iso_Open(const char* path);
void        Iso_Close(tIso_image* img);

/* List files in a slash-separated directory path within the image
   (e.g. "DATA/CUTSCENE").  Fills entries[] up to max_entries.
   Returns 0 on success, -1 if the directory was not found. */
int Iso_ListDir(tIso_image* img, const char* dir_path,
                tIso_entry* entries, int max_entries, int* count);

/* Read an entry's data and return it as a seekable FILE* (tmpfile-backed).
   Returns NULL on failure.  Caller closes the FILE* when done. */
FILE* Iso_ServeEntry(tIso_image* img, const tIso_entry* entry);

/* Scan dir for any file with a .GOG extension and write its full path
   into out (size out_size).  Returns 1 if found, 0 otherwise. */
int Iso_FindGogInDir(const char* dir, char* out, int out_size);

/* Resolve a slash-separated *file* path (e.g. "DATA/CARS/POLICE.TXT") within
   the image. Returns 1 and fills *out on success, 0 if any component of the
   path is missing or the leaf is a directory. */
int Iso_FindFile(tIso_image* img, const char* file_path, tIso_entry* out);

/* Resolve and serve a file path directly (Iso_FindFile + Iso_ServeEntry).
   Returns NULL if the path isn't found. */
FILE* Iso_Fopen(tIso_image* img, const char* file_path);

/* If path names a regular file that opens as a valid ISO 9660 image, return
   an open handle to it. Returns NULL if path is a directory, doesn't exist,
   or isn't a valid image -- callers should fall back to treating path as a
   plain directory in that case. */
tIso_image* Iso_OpenIfFile(const char* path);

/* --- Single active-image convenience layer for non-meld (Meld=0) mode --- */

/* If path names a regular file that opens as a valid ISO 9660 image, store
   it as the active single image and return 1. Otherwise return 0 and do
   nothing (no image is activated). At most one image is tracked; a later
   successful call replaces the previous one. */
int Iso_TrySetupSingle(const char* path);

/* Read-only fopen against the single active image (set up by
   Iso_TrySetupSingle). Returns NULL if no image is active, mode requests
   writing, or the path isn't found. Never handles writes -- ISO images are
   read-only media. */
FILE* Iso_FopenSingle(const char* pathname, const char* mode);

/* access(pathname, F_OK)-equivalent that also considers the single active
   image. Checks the real filesystem first (so an on-disk override always
   wins), then falls back to the image if no image is active or the disk
   check failed. Returns 0 if found, -1 otherwise -- a drop-in replacement
   for access(pathname, F_OK). */
int Iso_AccessSingle(const char* pathname);

/* Ensure ./DATA, ./DATA/SAVEGAME and ./DATA/SAVEGAME_M exist relative to the
   current working directory, creating any that are missing. A bare ISO image
   has no writable directories of its own, so save files need a real spot on
   disk. Safe to call unconditionally -- a no-op once the directories exist. */
void Iso_EnsureWritableDataDirs(void);

#endif /* HARNESS_ISO_H */
