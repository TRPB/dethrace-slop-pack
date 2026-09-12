#ifndef HARNESS_COCKPIT_H
#define HARNESS_COCKPIT_H

// Per-cockpit overrides for art this project generates, read from
// DATA/COCKPIT.TXT.
//
// The rear-view mirror rectangle is declared per CAR, in the
// resolution-dependent car TXT. That is the wrong granularity for widened
// cockpit art: nineteen cars share CKPT80, so one redrawn mirror would mean
// nineteen edited files that can drift apart. Worse, the rect cannot be
// derived -- the art is generated, so how far a mirror was continued past the
// 4:3 frame differs per cockpit and is only knowable by measuring the image.
//
// So the widened rect is declared once per cockpit image here, and applied to
// whichever car loads that image. The file is optional in every sense: absent
// file, absent entry, or unparseable line all leave the car TXT's own rect
// untouched, which is the original behaviour.
//
// Format, plain text:
//
//     # comments with # or //, blank lines ignored
//     CKPT80F.PIX  526,72,792,149
//
// Coordinates are in the same space as the car TXT's mirror line, so the
// values can be compared with the originals directly.

// Look up the mirror rect declared for a cockpit image. Returns 1 and fills
// the four outputs when one exists, 0 otherwise. pData_dir is the DATA
// directory (gApplication_path). Loads the file once, on first use.
int Cockpit_MirrorRect(const char* pData_dir, const char* pPix_name,
    int* pLeft, int* pTop, int* pRight, int* pBottom);

#endif
