// Added by dethrace
//
// The game's wheel models are low-poly cylinders built from two 8-vertex
// rings (16 vertices, 28 faces: 16 tread triangles + 6+6 end-cap fan
// triangles), which is why wheels render as visible octagons. This
// regenerates them into rounder cylinders in place, at car-load time.
//
// Wheel textures are NOT UV-wrapped consistently across the game: most cars
// use a planar projection (u/v = each vertex's (y,z) position normalized
// against the wheel's own bounding box - a "look straight down the axle"
// decal), but at least one (Hammer's HMRWHEEL.DAT) uses a genuine wrapped
// cylindrical mapping (u = position/circumference, v = 0 or 1 per ring).
// We detect which one a given ring uses (see TryPlanarProjection) by
// checking whether the planar formula reproduces the model's own stored
// UVs: if so we use that formula directly for every new vertex (it's a
// smooth curve, so this is exact - no faceting). Otherwise we fall back to
// interpolating between the two original ring vertices a new vertex falls
// between (wraparound-aware, in case the ring's own UVs wrap from 1.0 back
// to 0.0), which is exact for a linear wrap mapping like Hammer's.
//
// The two rings also aren't consistently laid out: retail-C1 wheel models
// store them as vertices 0-7 (front) / 8-15 (back), but at least one Splat
// Pack car (Lady Bug's BUFWHL.DAT) interleaves them throughout the array.
// See SplitIntoRings, which pairs vertices by geometry instead of index.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "brender.h"
#include "harness/config.h"
#include "loading.h"
#include "round_wheels.h"
#include "world.h"

#define ROUND_WHEEL_SEGMENTS 40
#define ORIG_RING_VERTS 8
// Upper bound on the number of angular segments an original wheel model may
// have (they are all 8 in the shipped data); bounds the fixed-size scratch
// arrays the general path below works with.
#define MAX_ORIG_SEGMENTS 64

// Added by dethrace
// UV parametrisation for a ring whose mapping wraps around the wheel rather
// than being projected onto it. Such a ring's UVs are constant at every one
// of its own vertices, because the model duplicates the ring at the seam and
// lets each segment span the whole texture (Monster Masher's tyre tread does
// this). Fitting UVs from those samples would say "constant", which at a
// higher segment count repeats the texture once per new segment instead of
// once per original segment. Instead the caller supplies the mapping
// explicitly as base + delta per original segment, and BuildRoundRing
// evaluates it continuously so the texture keeps its original density.
typedef struct tRing_wrap {
    br_scalar base[2];
    br_scalar delta[2];
} tRing_wrap;

// Added by dethrace
// Interpolates between two texture coordinates, taking the short way round
// if the pair straddles the 0.0/1.0 seam of a wrapped mapping.
//
// The result is deliberately NOT renormalised back into [0,1]: a coordinate
// of exactly 1.0 is the far edge of the texture, and folding it to 0.0
// teleports it to the near edge, which smears the whole texture across the
// triangle that spans them (visible as a hard line across the wheel).
// Values slightly outside [0,1] are fine - the renderer wraps them.
static br_scalar WrapDelta(br_scalar a, br_scalar b) {
    br_scalar diff = b - a;

    if (diff > 0.5f) {
        diff -= 1.0f;
    } else if (diff < -0.5f) {
        diff += 1.0f;
    }
    return diff;
}

static br_scalar LerpWrapped(br_scalar a, br_scalar b, br_scalar t) {
    return a + WrapDelta(a, b) * t;
}

// Added by dethrace
static br_uint_8 LerpU8(br_uint_8 a, br_uint_8 b, br_scalar t) {
    return (br_uint_8)((br_scalar)a + ((br_scalar)b - (br_scalar)a) * t);
}

// Added by dethrace
// Gaussian elimination with partial pivoting. Returns 0 if singular.
static int Solve3x3(double pM[3][3], const double* pRhs, double* pOut) {
    double m[3][3];
    double rhs[3];
    int i, j, k;

    memcpy(m, pM, sizeof(m));
    memcpy(rhs, pRhs, sizeof(rhs));

    for (i = 0; i < 3; i++) {
        int piv = i;
        for (j = i + 1; j < 3; j++) {
            if (fabs(m[j][i]) > fabs(m[piv][i])) {
                piv = j;
            }
        }
        if (fabs(m[piv][i]) < 1e-12) {
            return 0;
        }
        if (piv != i) {
            for (k = 0; k < 3; k++) {
                double t = m[i][k];
                m[i][k] = m[piv][k];
                m[piv][k] = t;
            }
            {
                double t = rhs[i];
                rhs[i] = rhs[piv];
                rhs[piv] = t;
            }
        }
        for (j = i + 1; j < 3; j++) {
            double factor = m[j][i] / m[i][i];
            for (k = i; k < 3; k++) {
                m[j][k] -= factor * m[i][k];
            }
            rhs[j] -= factor * rhs[i];
        }
    }
    for (i = 2; i >= 0; i--) {
        double s = rhs[i];
        for (j = i + 1; j < 3; j++) {
            s -= m[i][j] * pOut[j];
        }
        pOut[i] = s / m[i][i];
    }
    return 1;
}

// Added by dethrace
// Most wheels' UV is a planar projection: a "look straight down the axle"
// decal whose u,v are an affine function of the vertex's (y,z) position.
// The exact form varies between models though - axes can be swapped,
// flipped or offset (Eagle's WHEEL.DAT uses u=(z-zmin)/range, Big APC's
// BAPWHEEL.DAT the mirrored u=(zmax-z)/range, and so on), so testing for
// one hardcoded formula misclassifies most of them.
//
// Instead, least-squares fit the general affine map
//     u = a*y + b*z + c
//     v = d*y + e*z + f
// to the ring's own 8 stored UVs, which covers every flip/swap/rotation
// variant at once. If the fit reproduces all 8 within tolerance, the same
// map is applied to the newly generated vertices, which is exact at any
// segment count (no faceting, no seam). A genuinely wrapped cylindrical
// mapping - where u tracks angle rather than position, as on Hammer's
// HMRWHEEL.DAT - can't be expressed this way, so the fit fails and the
// caller falls back to interpolating between the original samples.
//
// Returns non-zero and fills the coefficient arrays on success.
static int TryAffineUV(br_vertex* old_ring, int pRing_len, double* pCoefU, double* pCoefV) {
    double m[3][3];
    double rhs_u[3];
    double rhs_v[3];
    int i, j, k;

    memset(m, 0, sizeof(m));
    memset(rhs_u, 0, sizeof(rhs_u));
    memset(rhs_v, 0, sizeof(rhs_v));

    // Accumulate the normal equations (M = B^T B, rhs = B^T x).
    for (i = 0; i < pRing_len; i++) {
        double b[3];
        b[0] = (double)old_ring[i].p.v[1];
        b[1] = (double)old_ring[i].p.v[2];
        b[2] = 1.0;
        for (j = 0; j < 3; j++) {
            for (k = 0; k < 3; k++) {
                m[j][k] += b[j] * b[k];
            }
            rhs_u[j] += b[j] * (double)old_ring[i].map.v[0];
            rhs_v[j] += b[j] * (double)old_ring[i].map.v[1];
        }
    }

    if (!Solve3x3(m, rhs_u, pCoefU) || !Solve3x3(m, rhs_v, pCoefV)) {
        return 0;
    }

    for (i = 0; i < pRing_len; i++) {
        double y = (double)old_ring[i].p.v[1];
        double z = (double)old_ring[i].p.v[2];
        double pu = pCoefU[0] * y + pCoefU[1] * z + pCoefU[2];
        double pv = pCoefV[0] * y + pCoefV[1] * z + pCoefV[2];
        if (fabs(pu - (double)old_ring[i].map.v[0]) > 0.02 || fabs(pv - (double)old_ring[i].map.v[1]) > 0.02) {
            return 0;
        }
    }
    return 1;
}

// Added by dethrace
// Splits the 16 original octagon vertices into front/back rings by
// geometry rather than index position. Every retail-C1 wheel model sampled
// stores them as vertices 0-7 (front) / 8-15 (back), but at least one Splat
// Pack car (Lady Bug's BUFWHL.DAT) interleaves them throughout the array
// instead - indexing by position there grabbed two vertices from the same
// side, collapsing the wheel to near-zero width.
//
// Each ring position has exactly one vertex on each side of the axle
// sharing the same (y,z), so vertices are paired by that instead, then the
// pairs are sorted by angle (pairing order isn't necessarily angular
// order). Returns 0 if the vertices don't cleanly pair up this way, so the
// caller can leave that model untouched rather than guess.
// orig_idx_a/orig_idx_b receive, for each output slot, which of the
// original 16 indices (0-15) ended up there - the caller needs this to
// classify faces referencing the original vertices by which ring they
// belong to (see ClassifyFaces).
static int SplitIntoRings(br_vertex* verts16, br_vertex* ring_a, br_vertex* ring_b, int* orig_idx_a, int* orig_idx_b) {
    int used[16];
    int slot = 0;
    int i, j;
    br_scalar angle[ORIG_RING_VERTS];

    memset(used, 0, sizeof(used));
    for (i = 0; i < 16; i++) {
        int partner = -1;
        if (used[i]) {
            continue;
        }
        for (j = i + 1; j < 16; j++) {
            if (!used[j] && fabs((double)(verts16[i].p.v[1] - verts16[j].p.v[1])) < 0.0001
                && fabs((double)(verts16[i].p.v[2] - verts16[j].p.v[2])) < 0.0001) {
                partner = j;
                break;
            }
        }
        if (partner < 0 || slot >= ORIG_RING_VERTS) {
            return 0;
        }
        used[i] = used[partner] = 1;
        if (verts16[i].p.v[0] <= verts16[partner].p.v[0]) {
            ring_a[slot] = verts16[i];
            ring_b[slot] = verts16[partner];
            orig_idx_a[slot] = i;
            orig_idx_b[slot] = partner;
        } else {
            ring_a[slot] = verts16[partner];
            ring_b[slot] = verts16[i];
            orig_idx_a[slot] = partner;
            orig_idx_b[slot] = i;
        }
        slot++;
    }
    if (slot != ORIG_RING_VERTS) {
        return 0;
    }

    // Insertion sort both rings, in lockstep, by angle.
    for (i = 0; i < ORIG_RING_VERTS; i++) {
        angle[i] = (br_scalar)atan2(ring_a[i].p.v[2], ring_a[i].p.v[1]);
    }
    for (i = 1; i < ORIG_RING_VERTS; i++) {
        br_scalar a = angle[i];
        br_vertex va = ring_a[i];
        br_vertex vb = ring_b[i];
        int oa = orig_idx_a[i];
        int ob = orig_idx_b[i];
        j = i - 1;
        while (j >= 0 && angle[j] > a) {
            angle[j + 1] = angle[j];
            ring_a[j + 1] = ring_a[j];
            ring_b[j + 1] = ring_b[j];
            orig_idx_a[j + 1] = orig_idx_a[j];
            orig_idx_b[j + 1] = orig_idx_b[j];
            j--;
        }
        angle[j + 1] = a;
        ring_a[j + 1] = va;
        ring_b[j + 1] = vb;
        orig_idx_a[j + 1] = oa;
        orig_idx_b[j + 1] = ob;
    }

    return 1;
}

// Added by dethrace
// Builds one ring of ROUND_WHEEL_SEGMENTS new vertices from the pRing_len
// original ring vertices starting at old_ring[0] (which must be in ascending
// angular order and evenly spaced), writing them into pOut (which must have
// room for ROUND_WHEEL_SEGMENTS entries).
//
// pWrap is normally NULL, meaning "work the UVs out from the ring's own
// samples". When non-NULL it supplies them instead - see tRing_wrap.
static void BuildRoundRing(br_vertex* old_ring, int pRing_len, br_scalar x, const tRing_wrap* pWrap, int pOut_count,
    br_vertex* pOut) {
    br_scalar angle0;
    br_scalar orig_radius[MAX_ORIG_SEGMENTS];
    double unwrapped_u[MAX_ORIG_SEGMENTS + 1];
    double unwrapped_v[MAX_ORIG_SEGMENTS + 1];
    br_scalar inradius_scale;
    double coef_u[3];
    double coef_v[3];
    int is_affine;
    int i;

    angle0 = (br_scalar)atan2(old_ring[0].p.v[2], old_ring[0].p.v[1]);
    for (i = 0; i < pRing_len; i++) {
        orig_radius[i] = (br_scalar)sqrt(old_ring[i].p.v[1] * old_ring[i].p.v[1] + old_ring[i].p.v[2] * old_ring[i].p.v[2]);
    }
    is_affine = pWrap == NULL && TryAffineUV(old_ring, pRing_len, coef_u, coef_v);

    // Running total of the ring's own texture coordinates, each step taking
    // the short way round the 0/1 seam, so the sequence stays continuous
    // instead of snapping back to each original sample's absolute value.
    //
    // Interpolating between neighbouring samples pairwise is not enough: on a
    // wrapped mapping like Hammer's, u descends 0.125, 0.0 and then the next
    // sample is 0.875. Taking the short way makes the run continue to -0.125,
    // but the following pair re-anchors at +0.875, so one face has to sweep
    // almost the whole texture backwards to get there. Accumulating instead
    // lets the run carry on past the seam, and the renderer wraps it.
    unwrapped_u[0] = (double)old_ring[0].map.v[0];
    unwrapped_v[0] = (double)old_ring[0].map.v[1];
    for (i = 1; i <= pRing_len; i++) {
        int prev = i - 1;
        int next = i % pRing_len;

        unwrapped_u[i] = unwrapped_u[prev]
            + (double)WrapDelta(old_ring[prev].map.v[0], old_ring[next].map.v[0]);
        unwrapped_v[i] = unwrapped_v[prev]
            + (double)WrapDelta(old_ring[prev].map.v[1], old_ring[next].map.v[1]);
    }

    // Build the ring on the original polygon's *inscribed* circle, not through
    // its vertices.
    //
    // The vertices sit at the circumradius, but the polygon's surface only
    // reaches that at the vertex angles - between them it cuts in to
    // R*cos(pi/n). Running the new ring through the vertices therefore makes
    // the wheel occupy more space than the original everywhere else, up to
    // 7.6% more radius for an octagon, and the bottom of the wheel sinks into
    // the road by that much (~4cm on a typical wheel). It went unnoticed on the
    // original mesh because a spinning polygon's lowest point oscillates
    // between the two radii, so the dip flickered; a circle holds it, leaving a
    // permanent flat where the road cuts the tyre.
    //
    // The inscribed circle lies inside the original outline at every angle, so
    // the regenerated wheel can never protrude where the original didn't. This
    // is purely cosmetic: the simulation takes wheel contact from the car's own
    // wpos[]/susp_height (car.c MultiFindFloorInBoxM) and never consults the
    // mesh, so nothing here moves the car or changes how it drives.
    inradius_scale = (br_scalar)cos(DR_PI / (double)pRing_len);

    for (i = 0; i < pOut_count; i++) {
        br_scalar frac_index = (br_scalar)i * ((br_scalar)pRing_len / (br_scalar)ROUND_WHEEL_SEGMENTS);
        int k = (int)frac_index;
        br_scalar t = frac_index - (br_scalar)k;
        int k1;
        br_scalar radius;

        // The closing vertex (i == ROUND_WHEEL_SEGMENTS) lands exactly on the
        // last original sample; keep it in range and let t carry it there.
        if (k >= pRing_len) {
            k = pRing_len - 1;
            t = 1.0f;
        }
        k1 = (k + 1) % pRing_len;
        radius = (orig_radius[k] + (orig_radius[k1] - orig_radius[k]) * t) * inradius_scale;
        br_scalar angle = angle0 + (br_scalar)i * ((br_scalar)(2.0 * DR_PI) / (br_scalar)ROUND_WHEEL_SEGMENTS);
        br_scalar y = radius * (br_scalar)cos(angle);
        br_scalar z = radius * (br_scalar)sin(angle);

        pOut[i].p.v[0] = x;
        pOut[i].p.v[1] = y;
        pOut[i].p.v[2] = z;
        if (pWrap != NULL) {
            // frac_index is the position along the original ring measured in
            // original segments, which is exactly the parameter the wrapped
            // mapping advances by delta over.
            pOut[i].map.v[0] = pWrap->base[0] + pWrap->delta[0] * frac_index;
            pOut[i].map.v[1] = pWrap->base[1] + pWrap->delta[1] * frac_index;
        } else if (is_affine) {
            pOut[i].map.v[0] = (br_scalar)(coef_u[0] * (double)y + coef_u[1] * (double)z + coef_u[2]);
            pOut[i].map.v[1] = (br_scalar)(coef_v[0] * (double)y + coef_v[1] * (double)z + coef_v[2]);
        } else {
            pOut[i].map.v[0] = (br_scalar)(unwrapped_u[k] + (unwrapped_u[k + 1] - unwrapped_u[k]) * (double)t);
            pOut[i].map.v[1] = (br_scalar)(unwrapped_v[k] + (unwrapped_v[k + 1] - unwrapped_v[k]) * (double)t);
        }
        // Prelit vertex colour (index/red/grn/blu). A BR_MATF_PRELIT
        // material shades from these, so they can't be left as the zeroes
        // RoundOffWheelModel's memset put there. Carry the originals across
        // by interpolating between the two neighbouring source vertices,
        // the same way the non-affine UV path does.
        pOut[i].index = LerpU8(old_ring[k].index, old_ring[k1].index, t);
        pOut[i].red = LerpU8(old_ring[k].red, old_ring[k1].red, t);
        pOut[i].grn = LerpU8(old_ring[k].grn, old_ring[k1].grn, t);
        pOut[i].blu = LerpU8(old_ring[k].blu, old_ring[k1].blu, t);
    }
}

// Added by dethrace
// Appends a cap made of a fan of `count` triangles from pivot_idx to
// consecutive ring vertices, starting at ring position first_a (mod
// ring_len each step, so it can wrap around). Hub-style caps pivot on a
// separate hub vertex and use first_a=0, count=ring_len (a full closed
// fan). Corner-style caps pivot on the ring's own vertex 0 and use
// first_a=1, count=ring_len-2 (an open fan that skips the pivot's own
// position). Winding is worked out from the candidate normal's direction
// rather than assumed, so it works for either style and either ring:
// outward_x is the sign the cap's normal should have along the axle, and the
// triangles are emitted whichever way round produces it.
static void AppendCapFan(br_face* faces, int* pF, int pivot_idx, int ring_start, int ring_len, int first_a, int count,
    br_vertex* verts, br_scalar outward_x, br_uint_16 smoothing, br_face* template_face) {
    br_vector3 edge1, edge2, normal;
    int reverse;
    int a0 = ring_start + (first_a % ring_len);
    int b0 = ring_start + ((first_a + 1) % ring_len);
    int i;

    BrVector3Sub(&edge1, &verts[a0].p, &verts[pivot_idx].p);
    BrVector3Sub(&edge2, &verts[b0].p, &verts[pivot_idx].p);
    BrVector3Cross(&normal, &edge1, &edge2);
    reverse = (normal.v[0] > 0) != (outward_x > 0);

    for (i = 0; i < count; i++) {
        int a = ring_start + ((first_a + i) % ring_len);
        int b = ring_start + ((first_a + i + 1) % ring_len);
        if (template_face != NULL) {
            // Carries the original material, prelit colours and flags over
            // to the regenerated geometry.
            faces[*pF] = *template_face;
        }
        faces[*pF].vertices[0] = (br_uint_16)pivot_idx;
        faces[*pF].vertices[1] = (br_uint_16)(reverse ? b : a);
        faces[*pF].vertices[2] = (br_uint_16)(reverse ? a : b);
        // A smoothing value of 0 is not "no smoothing group" - BRender's
        // prepare step (prepmesh.c) treats it as "smooth with everything",
        // which would blend the cap's normal into the tread's at every
        // shared ring vertex. Callers therefore pass a bit the surrounding
        // bands don't use. All the cap faces on one ring are coplanar, so
        // smoothing them together is a no-op for the resulting normal.
        faces[*pF].smoothing = smoothing;
        (*pF)++;
    }
}

// Added by dethrace
// Classifies every face in the original model by which of its vertices are
// "ring" vertices (one of the 16 original octagon vertices, tagged 0/1 by
// which ring via ring_of[]) vs "extra" ones (index >= 16):
//   - 0 extra vertices (tread faces spanning both rings, or corner-pivot
//     cap faces using only one ring's own vertices): ignored - both get
//     fully regenerated from scratch, so the originals aren't needed.
//   - 2 ring vertices from the SAME ring + 1 extra vertex sitting at
//     (near) zero radius: that ring uses a hub-center fan cap (e.g.
//     NEWEAGLE's EAFLWHL.DAT), and the extra vertex is that hub.
//   - 3 extra vertices, no ring vertices: geometry unrelated to the rings,
//     which splits two ways depending on where those vertices sit. If they
//     stand somewhere of their own it is decoration (e.g. Val Hella's valve
//     stem) and is preserved as-is, remapped. If all three sit exactly on
//     ring vertices it is a duplicate of tread we are about to regenerate -
//     Splat Pack's XJBWHL.DAT holds one such triangle, three copies of ring
//     vertices carrying their own texture coordinates for the seam - and it
//     is dropped. Preserving it instead leaves an octagon-sized triangle
//     stranded inside the finished round wheel.
// Anything else - an extra vertex that isn't near zero radius (so not a
// hub), mixed in with ring vertices in some other pattern - means a
// topology we don't recognize (e.g. Hammer's HMRWHEEL.DAT, which has a
// second, non-hub ring of extra vertices for a rim lip). Returns 0 in that
// case so the caller can leave the model untouched entirely, rather than
// silently drop geometry it doesn't understand.
//
// Also picks out one representative original face per region (tread, cap on
// ring A, cap on ring B). The caller seeds each regenerated face from these
// so the new geometry keeps the original's material, prelit colours and
// flags. This matters a lot: for most cars the wheel texture comes from
// br_face::material on the model, NOT from the wheel actor's material
// (Eagle is the odd one out, with BGLWEEL.MAT set on the actor and no
// per-face materials at all). Losing them renders the wheel untextured.
static int ClassifyFaces(br_model* pModel, int* ring_of, int* at_ring_position, int* face_is_decor,
    int* hub_extra_idx_a, int* hub_extra_idx_b, br_face** tread_face, br_face** cap_a_face, br_face** cap_b_face) {
    int i, j;

    *hub_extra_idx_a = -1;
    *hub_extra_idx_b = -1;
    *tread_face = NULL;
    *cap_a_face = NULL;
    *cap_b_face = NULL;
    for (i = 0; i < pModel->nfaces; i++) {
        br_face* face = &pModel->faces[i];
        int lo_count = 0;
        int ring_membership = -2;
        int extra_idx = -1;
        int extra_count = 0;
        int on_ring_count = 0;

        face_is_decor[i] = 0;
        for (j = 0; j < 3; j++) {
            int v = face->vertices[j];
            if (v < 16) {
                int r = ring_of[v];
                lo_count++;
                if (ring_membership == -2) {
                    ring_membership = r;
                } else if (ring_membership != r) {
                    ring_membership = -1;
                }
            } else {
                extra_idx = v;
                extra_count++;
                if (at_ring_position[v]) {
                    on_ring_count++;
                }
            }
        }

        if (extra_count == 0) {
            // Ring vertices only: spans both rings => tread, otherwise a
            // corner-pivot cap on whichever ring it sits in.
            if (ring_membership == -1) {
                if (*tread_face == NULL) {
                    *tread_face = face;
                }
            } else if (ring_membership == 0) {
                if (*cap_a_face == NULL) {
                    *cap_a_face = face;
                }
            } else if (ring_membership == 1) {
                if (*cap_b_face == NULL) {
                    *cap_b_face = face;
                }
            }
            continue;
        }
        if (extra_count == 3) {
            // Only real decoration is worth carrying over; a triangle whose
            // three corners all duplicate ring vertices is tread we are
            // about to regenerate anyway.
            face_is_decor[i] = on_ring_count != 3;
            continue;
        }
        if (lo_count == 2 && extra_count == 1 && ring_membership >= 0) {
            br_vertex* ev = &pModel->vertices[extra_idx];
            br_scalar r2 = ev->p.v[1] * ev->p.v[1] + ev->p.v[2] * ev->p.v[2];
            if (r2 < 0.0001f) {
                if (ring_membership == 0) {
                    *hub_extra_idx_a = extra_idx;
                    if (*cap_a_face == NULL) {
                        *cap_a_face = face;
                    }
                } else {
                    *hub_extra_idx_b = extra_idx;
                    if (*cap_b_face == NULL) {
                        *cap_b_face = face;
                    }
                }
                continue;
            }
        }
        // Unrecognized pattern.
        return 0;
    }
    return 1;
}

// Added by dethrace
// Builds a rounder replacement mesh for a low-poly octagonal wheel model
// made of two 8-vertex rings. Returns 0 (writing nothing) if the model isn't
// that shape, leaving the caller to try the general path below.
//
// Models are shared by identifier across many cars (e.g. several cars
// reference the same WHEEL.DAT), and this can be called once per car that
// references a given model, so it must be idempotent: SplitIntoRings only
// succeeds when the first 16 vertices cleanly pair into two 8-vertex rings,
// which an already-rounded model's first 16 vertices (16 of one ring's now
// much-larger vertex count) never do, so repeat calls on an already-rounded
// model are naturally a no-op. Using a plain vertex/face count check here
// previously caused reprocessing of already-rounded models, corrupting them
// (garbled/black textures, occasional crashes).
static int BuildTwoRingMesh(br_model* pModel, br_vertex** pOut_verts, int* pOut_nvertices, br_face** pOut_faces,
    int* pOut_nfaces) {
    br_vertex old_verts[16];
    br_vertex ring_a[ORIG_RING_VERTS], ring_b[ORIG_RING_VERTS];
    int orig_idx_a[ORIG_RING_VERTS], orig_idx_b[ORIG_RING_VERTS];
    int ring_of[16];
    int* at_ring_position;
    int* face_is_decor;
    br_scalar x_front, x_back;
    int hub_extra_idx_a, hub_extra_idx_b;
    int hub_a, hub_b;
    int classified;
    int* remap;
    br_vertex* decor_verts;
    int decor_nverts;
    int decor_nfaces;
    int hub_a_out = -1, hub_b_out = -1;
    int decor_start;
    int new_nvertices, new_nfaces;
    int front_cap_count, back_cap_count;
    br_vertex* new_verts;
    br_face* new_faces;
    br_face* tread_face;
    br_face* cap_a_face;
    br_face* cap_b_face;
    br_face tread_template;
    br_face cap_a_template;
    br_face cap_b_template;
    int have_tread_template;
    int i, f;
    const int n = ROUND_WHEEL_SEGMENTS;

    if (pModel->nvertices < 16 || pModel->nfaces < 16) {
        return 0;
    }
    memcpy(old_verts, pModel->vertices, sizeof(old_verts));
    if (!SplitIntoRings(old_verts, ring_a, ring_b, orig_idx_a, orig_idx_b)) {
        // Vertices don't cleanly pair into two rings (either a shape we
        // don't recognize, or this model has already been rounded); leave
        // it as-is rather than guess.
        return 0;
    }
    for (i = 0; i < ORIG_RING_VERTS; i++) {
        ring_of[orig_idx_a[i]] = 0;
        ring_of[orig_idx_b[i]] = 1;
    }
    x_front = ring_a[0].p.v[0];
    x_back = ring_b[0].p.v[0];

    // Flag every extra vertex that stands exactly where a ring vertex does.
    // Those are copies a model keeps so one corner of the octagon can carry
    // two different sets of texture coordinates, not geometry of its own.
    at_ring_position = BrMemAllocate(sizeof(int) * pModel->nvertices, BR_MEMORY_APPLICATION);
    face_is_decor = BrMemAllocate(sizeof(int) * pModel->nfaces, BR_MEMORY_APPLICATION);
    for (i = 0; i < pModel->nvertices; i++) {
        int j;

        at_ring_position[i] = 0;
        for (j = 0; j < 16; j++) {
            if (fabs((double)(pModel->vertices[i].p.v[0] - old_verts[j].p.v[0])) < 0.0001
                && fabs((double)(pModel->vertices[i].p.v[1] - old_verts[j].p.v[1])) < 0.0001
                && fabs((double)(pModel->vertices[i].p.v[2] - old_verts[j].p.v[2])) < 0.0001) {
                at_ring_position[i] = 1;
                break;
            }
        }
    }

    classified = ClassifyFaces(pModel, ring_of, at_ring_position, face_is_decor, &hub_extra_idx_a, &hub_extra_idx_b,
        &tread_face, &cap_a_face, &cap_b_face);
    BrMemFree(at_ring_position);
    if (!classified) {
        // Some face references extra geometry in a pattern we don't
        // recognize (not a hub, not pure decoration); leave this model
        // untouched rather than silently drop geometry we don't understand.
        BrMemFree(face_is_decor);
        return 0;
    }
    hub_a = hub_extra_idx_a >= 0;
    hub_b = hub_extra_idx_b >= 0;

    // Snapshot the representative faces before the originals are freed, so
    // regenerated geometry inherits the original material/colours/flags.
    // Fall back to the tread face for a cap (and vice versa) if a region
    // had no representative, so a face never ends up with a NULL material
    // when the model did have one.
    if (tread_face == NULL) {
        tread_face = cap_a_face != NULL ? cap_a_face : cap_b_face;
    }
    have_tread_template = tread_face != NULL;
    if (cap_a_face == NULL) {
        cap_a_face = tread_face;
    }
    if (cap_b_face == NULL) {
        cap_b_face = tread_face;
    }
    if (tread_face != NULL) {
        tread_template = *tread_face;
    }
    if (cap_a_face != NULL) {
        cap_a_template = *cap_a_face;
    }
    if (cap_b_face != NULL) {
        cap_b_template = *cap_b_face;
    }

    // Collect the vertices the decorative faces use, so nothing the wheel
    // doesn't actually draw gets carried into the new model.
    remap = BrMemAllocate(sizeof(int) * pModel->nvertices, BR_MEMORY_APPLICATION);
    decor_verts = BrMemAllocate(sizeof(br_vertex) * pModel->nvertices, BR_MEMORY_APPLICATION);
    decor_nverts = 0;
    decor_nfaces = 0;
    for (i = 0; i < pModel->nvertices; i++) {
        remap[i] = -1;
    }
    for (i = 0; i < pModel->nfaces; i++) {
        int j;

        if (!face_is_decor[i]) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            int v = pModel->faces[i].vertices[j];

            if (remap[v] < 0) {
                decor_verts[decor_nverts] = pModel->vertices[v];
                remap[v] = decor_nverts;
                decor_nverts++;
            }
        }
        decor_nfaces++;
    }

    front_cap_count = hub_a ? n : (n - 2);
    back_cap_count = hub_b ? n : (n - 2);
    hub_a_out = hub_a ? n * 2 : -1;
    hub_b_out = hub_b ? n * 2 + (hub_a ? 1 : 0) : -1;
    decor_start = n * 2 + (hub_a ? 1 : 0) + (hub_b ? 1 : 0);

    new_nvertices = decor_start + decor_nverts;
    new_nfaces = (n * 2) + front_cap_count + back_cap_count + decor_nfaces;
    new_verts = BrResAllocate(pModel, sizeof(br_vertex) * new_nvertices, BR_MEMORY_VERTICES);
    new_faces = BrResAllocate(pModel, sizeof(br_face) * new_nfaces, BR_MEMORY_FACES);
    memset(new_verts, 0, sizeof(br_vertex) * new_nvertices);
    memset(new_faces, 0, sizeof(br_face) * new_nfaces);

    // Wheel models are authored with X as the axle axis (true of every
    // wheel model in the game's data); ring_a/ring_b were split out by
    // geometry above.
    BuildRoundRing(ring_a, ORIG_RING_VERTS, x_front, NULL, n, &new_verts[0]);
    BuildRoundRing(ring_b, ORIG_RING_VERTS, x_back, NULL, n, &new_verts[n]);
    // Copy the hub vertices verbatim - in particular do NOT snap their x
    // onto the ring's plane. Several wheels (e.g. Splat Pack Eagle's
    // EAFLWHL.DAT, rings at x=+/-0.0417 with its hub at x=-0.0028) recess
    // the hub inwards, and that offset is exactly what makes the wheel face
    // dished/concave rather than flat. Overwriting x flattens it into a
    // plain cylinder.
    if (hub_a) {
        new_verts[hub_a_out] = pModel->vertices[hub_extra_idx_a];
    }
    if (hub_b) {
        new_verts[hub_b_out] = pModel->vertices[hub_extra_idx_b];
    }
    for (i = 0; i < decor_nverts; i++) {
        new_verts[decor_start + i] = decor_verts[i];
    }
    BrMemFree(decor_verts);

    f = 0;
    for (i = 0; i < n; i++) {
        int next = (i + 1) % n;

        if (have_tread_template) {
            new_faces[f] = tread_template;
        }
        new_faces[f].vertices[0] = (br_uint_16)i;
        new_faces[f].vertices[1] = (br_uint_16)next;
        new_faces[f].vertices[2] = (br_uint_16)(n + next);
        new_faces[f].smoothing = 1;
        f++;

        if (have_tread_template) {
            new_faces[f] = tread_template;
        }
        new_faces[f].vertices[0] = (br_uint_16)i;
        new_faces[f].vertices[1] = (br_uint_16)(n + next);
        new_faces[f].vertices[2] = (br_uint_16)(n + i);
        new_faces[f].smoothing = 1;
        f++;
    }

    // End caps. Corner-pivot fans from ring vertex 0 (n-2 triangles); hub
    // fans wrap all the way around from the new hub vertex (n triangles).
    AppendCapFan(new_faces, &f, hub_a ? hub_a_out : 0, 0, n, hub_a ? 0 : 1, front_cap_count, new_verts,
        x_front - x_back, 2, cap_a_face != NULL ? &cap_a_template : NULL);
    AppendCapFan(new_faces, &f, hub_b ? hub_b_out : n, n, n, hub_b ? 0 : 1, back_cap_count, new_verts,
        x_back - x_front, 2, cap_b_face != NULL ? &cap_b_template : NULL);

    for (i = 0; i < pModel->nfaces; i++) {
        br_face* face = &pModel->faces[i];
        if (face_is_decor[i]) {
            // Copy the whole face (material, prelit colours, smoothing,
            // flags), then rewrite just the vertex indices.
            new_faces[f] = *face;
            new_faces[f].vertices[0] = (br_uint_16)(decor_start + remap[face->vertices[0]]);
            new_faces[f].vertices[1] = (br_uint_16)(decor_start + remap[face->vertices[1]]);
            new_faces[f].vertices[2] = (br_uint_16)(decor_start + remap[face->vertices[2]]);
            f++;
        }
    }
    BrMemFree(face_is_decor);
    BrMemFree(remap);

    *pOut_verts = new_verts;
    *pOut_nvertices = new_nvertices;
    *pOut_faces = new_faces;
    *pOut_nfaces = new_nfaces;
    return 1;
}

// ---------------------------------------------------------------------------
// General path: wheels built as a surface of revolution.
//
// The two-ring generator above only understands a plain cylinder, which is
// what most of the game's wheels are. The rest are still lathe-turned shapes,
// just with more than two rings in their profile:
//
//   - Roadhog/Helga's and Splat Pack Vlad's rear wheels (RDRWHL.DAT,
//     Vdrwhl.dat, VDRRWHL.DAT: 32v/60f) are four concentric rings - tyre
//     back, tyre front, rim, and the same rim radius recessed inwards to
//     dish it.
//   - Monster Masher's (Monwhl.dat, Mnwhlr.dat: 72v/76f) are a torus: a
//     five-position profile bulging out either side of the tread.
//
// Both are the same thing at heart - N evenly spaced angular slices, each
// holding one vertex per profile position - so rather than special-casing
// either, this rebuilds any model of that form at ROUND_WHEEL_SEGMENTS
// slices. It also subsumes the two-ring case, but that generator is left in
// front of it because it additionally handles decorative geometry welded onto
// the wheel (Val Hella's valve stem) and is what all 260-odd cylinder wheels
// in the shipped data already go through.
//
// The work is: sort the vertices into slices, find the permutation that
// rotates each slice onto the next (so each profile position's vertices form
// one orbit = one ring), then rebuild - regenerating each ring at the new
// segment count, replicating the band faces that join adjacent slices, and
// re-fanning the caps that close a ring off.
// ---------------------------------------------------------------------------

// A vertex this close to the axle is treated as sitting *on* it (a hub), so
// it has no meaningful angle and belongs to no slice.
#define REV_POLE_RADIUS 0.01f
// ~1 degree. Slices in the shipped models are 45 degrees apart.
#define REV_ANGLE_TOL 0.017
#define REV_POS_TOL 0.001
#define REV_UV_TOL 0.002
// How far a wheel must depart from mirror symmetry about its axle before it is
// worth giving the left side its own mirrored copy. Set from measurement -- see
// wheels.md; a tiny departure is modelling noise, not a visible dish.
#define MIRROR_ASYMMETRY_MIN 0.01
// Distinct materials tracked per vertex; wheels use one or two.
#define MAX_VERTEX_MATERIALS 4

// Added by dethrace
typedef struct tRev_analysis {
    int nslices;
    int nslots;
    int npoles;
    int* slice_of; // [nvertices] which angular slice, or -1 for a hub vertex
    int* slot_of;  // [nvertices] which profile position, or -1 for a hub vertex
    int* orbit;    // [nslots][nslices] -> original vertex index
} tRev_analysis;

// Added by dethrace
// Scratch that only AnalyseRevolution needs, split out so it can use plain
// early returns and have its caller do the freeing.
typedef struct tRev_scratch {
    double* angle;
    double* radius;
    int* order;
    int* sigma; // rotate-one-slice permutation being built
    int* claimed;
    br_material** materials; // [nvertices][MAX_VERTEX_MATERIALS]
    int* material_count;     // [nvertices]
} tRev_scratch;

// Added by dethrace
// One face of the repeating angular period, expressed as (which ring, which
// end of the segment) so it can be stamped out at any segment count.
typedef struct tRev_band {
    int slot[3];
    int offset[3]; // 0 = this slice, 1 = the next one round
    int group;     // new smoothing group index
    br_face face;  // template: material, prelit colours, flags
} tRev_band;

// Added by dethrace
// The disc that closes one ring off, either fanned from a hub vertex on the
// axle or from one of the ring's own vertices.
typedef struct tRev_cap {
    int nfaces;
    int pole;       // hub vertex index, or -1 for a corner-pivot fan
    double outward; // sign along the axle that the cap should face
    br_face face;   // template
} tRev_cap;

// Added by dethrace
typedef struct tRev_build {
    tRev_analysis a;
    int* face_kind;   // [nfaces] 0 = band, 1 = cap
    int* face_key;    // [nfaces] band: base slice; cap: slot
    tRev_band* band;  // [nfaces], first nbands entries used
    int nbands;       // band faces per angular period
    int* band_used;   // [nfaces]
    tRev_cap* cap;    // [nslots]
    tRing_wrap* wrap; // [nslots]
    int* has_wrap;    // [nslots]
    int* pole_out;    // [nvertices] hub vertex -> index in the new mesh
} tRev_build;

// Added by dethrace
// Records, for each vertex, the distinct materials of the faces using it.
//
// This is what a vertex belongs to rather than where it sits, and it survives
// rotation, so it can tell apart two rings a model keeps at the same place on
// the profile for two different surfaces of the wheel. Saturating at
// MAX_VERTEX_MATERIALS only makes the comparison coarser, never wrong in a way
// that matters - a wheel uses one or two materials.
static void BuildVertexMaterialSets(br_model* pModel, br_material** pSets, int* pCounts) {
    int i, j, k;

    for (i = 0; i < pModel->nvertices; i++) {
        pCounts[i] = 0;
    }
    for (i = 0; i < pModel->nfaces; i++) {
        for (j = 0; j < 3; j++) {
            int v = pModel->faces[i].vertices[j];

            for (k = 0; k < pCounts[v]; k++) {
                if (pSets[v * MAX_VERTEX_MATERIALS + k] == pModel->faces[i].material) {
                    break;
                }
            }
            if (k == pCounts[v] && pCounts[v] < MAX_VERTEX_MATERIALS) {
                pSets[v * MAX_VERTEX_MATERIALS + pCounts[v]] = pModel->faces[i].material;
                pCounts[v]++;
            }
        }
    }
}

// Added by dethrace
static int VertexMaterialSetsMatch(br_material** pSets, int* pCounts, int a, int b) {
    int i, j;

    if (pCounts[a] != pCounts[b]) {
        return 0;
    }
    for (i = 0; i < pCounts[a]; i++) {
        for (j = 0; j < pCounts[b]; j++) {
            if (pSets[a * MAX_VERTEX_MATERIALS + i] == pSets[b * MAX_VERTEX_MATERIALS + j]) {
                break;
            }
        }
        if (j == pCounts[b]) {
            return 0;
        }
    }
    return 1;
}

// Added by dethrace
// Difference between two angles, brought back into [-pi, pi].
static double AngleDelta(double a, double b) {
    double d = a - b;

    while (d > DR_PI) {
        d -= 2.0 * DR_PI;
    }
    while (d < -DR_PI) {
        d += 2.0 * DR_PI;
    }
    return d;
}

// Added by dethrace
static double RingRadius(br_vertex* pVertex) {
    double y = (double)pVertex->p.v[1];
    double z = (double)pVertex->p.v[2];

    return sqrt(y * y + z * z);
}

// Added by dethrace
// Sorts the model's vertices into evenly spaced angular slices and works out
// the permutation that rotates each slice onto the next, which is what turns
// a flat vertex array into "P rings of N vertices". Returns 0 if the model
// isn't such a shape, in which case the caller leaves it alone.
static int AnalyseRevolution(br_model* pModel, tRev_analysis* pRev, tRev_scratch* pS) {
    int nv = pModel->nvertices;
    double slice_angle[MAX_ORIG_SEGMENTS];
    int slice_count[MAX_ORIG_SEGMENTS];
    int nspin = 0;
    int slot;
    int i, j, s;
    double step;

    for (i = 0; i < nv; i++) {
        pS->radius[i] = RingRadius(&pModel->vertices[i]);
        pS->angle[i] = atan2((double)pModel->vertices[i].p.v[2], (double)pModel->vertices[i].p.v[1]);
        pS->sigma[i] = -1;
        pS->claimed[i] = 0;
        pRev->slice_of[i] = -1;
        pRev->slot_of[i] = -1;
        if (pS->radius[i] < REV_POLE_RADIUS) {
            pRev->npoles++;
        } else {
            pS->order[nspin] = i;
            nspin++;
        }
    }
    if (nspin < 6) {
        return 0;
    }

    // Insertion sort by angle - wheel models run to a few hundred vertices.
    for (i = 1; i < nspin; i++) {
        int v = pS->order[i];

        j = i - 1;
        while (j >= 0 && pS->angle[pS->order[j]] > pS->angle[v]) {
            pS->order[j + 1] = pS->order[j];
            j--;
        }
        pS->order[j + 1] = v;
    }

    // Cut the sorted angles into slices wherever there is a gap.
    pRev->nslices = 0;
    for (i = 0; i < nspin; i++) {
        if (i == 0 || pS->angle[pS->order[i]] - pS->angle[pS->order[i - 1]] > REV_ANGLE_TOL) {
            if (pRev->nslices == MAX_ORIG_SEGMENTS) {
                return 0;
            }
            slice_angle[pRev->nslices] = pS->angle[pS->order[i]];
            slice_count[pRev->nslices] = 0;
            pRev->nslices++;
        }
        pRev->slice_of[pS->order[i]] = pRev->nslices - 1;
        slice_count[pRev->nslices - 1]++;
    }
    // atan2 cuts the circle at +/-pi, so a slice landing on that cut comes
    // out as two: fold the last group back into the first if they are really
    // the same angle.
    if (pRev->nslices >= 2 && fabs(AngleDelta(slice_angle[pRev->nslices - 1], slice_angle[0])) < REV_ANGLE_TOL) {
        for (i = 0; i < nv; i++) {
            if (pRev->slice_of[i] == pRev->nslices - 1) {
                pRev->slice_of[i] = 0;
            }
        }
        slice_count[0] += slice_count[pRev->nslices - 1];
        pRev->nslices--;
    }

    if (pRev->nslices < 3 || pRev->nslices >= ROUND_WHEEL_SEGMENTS) {
        // Fewer than three slices isn't a wheel; at or above the target
        // count there is nothing to round off, which is also what makes
        // running this over an already-rounded model a no-op.
        return 0;
    }
    if (nspin % pRev->nslices != 0) {
        return 0;
    }
    pRev->nslots = nspin / pRev->nslices;
    for (s = 0; s < pRev->nslices; s++) {
        if (slice_count[s] != pRev->nslots) {
            return 0;
        }
    }
    // The slices must be evenly spaced, or one rotation step can't map each
    // onto the next.
    step = 2.0 * DR_PI / (double)pRev->nslices;
    for (s = 1; s < pRev->nslices; s++) {
        if (fabs(AngleDelta(slice_angle[s], slice_angle[0] + (double)s * step)) > REV_ANGLE_TOL) {
            return 0;
        }
    }

    // Pair each slice's vertices up with the next slice's. Position alone is
    // usually enough, but a model can hold several vertices at one place on
    // the profile that differ only in mapping - that is how a wheel whose
    // texture wraps around it duplicates the seam, and how one built from two
    // materials keeps a separate copy of a shared ring for each.
    //
    // So there are three passes, each narrower than the last, and each only
    // commits where the answer is unique:
    //   0. same mapping - separates the copies of a ring that a wrapped
    //      texture leaves behind, whose mapping is constant all the way round.
    //   1. same set of face materials - separates copies belonging to
    //      different surfaces of the wheel, whose mapping does vary. Hammer's
    //      HMRWHEEL.DAT needs this: its tread ring and the ring the wheel
    //      face is fanned from sit at exactly the same place, and only the
    //      material tells them apart.
    //   2. position alone.
    // If it still isn't unique, give up rather than pick arbitrarily.
    for (s = 0; s < pRev->nslices; s++) {
        int next = (s + 1) % pRev->nslices;
        int pass;

        for (pass = 0; pass < 3; pass++) {
            for (i = 0; i < nv; i++) {
                int cand = -1;
                int ncand = 0;

                if (pRev->slice_of[i] != s || pS->sigma[i] >= 0) {
                    continue;
                }
                for (j = 0; j < nv; j++) {
                    if (pRev->slice_of[j] != next || pS->claimed[j]) {
                        continue;
                    }
                    if (fabs((double)(pModel->vertices[i].p.v[0] - pModel->vertices[j].p.v[0])) > REV_POS_TOL
                        || fabs(pS->radius[i] - pS->radius[j]) > REV_POS_TOL) {
                        continue;
                    }
                    if (pass == 0
                        && (fabs((double)(pModel->vertices[i].map.v[0] - pModel->vertices[j].map.v[0])) > REV_UV_TOL
                            || fabs((double)(pModel->vertices[i].map.v[1] - pModel->vertices[j].map.v[1]))
                                > REV_UV_TOL)) {
                        continue;
                    }
                    if (pass == 1 && !VertexMaterialSetsMatch(pS->materials, pS->material_count, i, j)) {
                        continue;
                    }
                    cand = j;
                    ncand++;
                }
                if (ncand == 1) {
                    pS->sigma[i] = cand;
                    pS->claimed[cand] = 1;
                } else if (pass == 2) {
                    return 0;
                }
            }
        }
    }

    // Walk each of the first slice's vertices all the way round; each closed
    // walk is one ring of the profile.
    pRev->orbit = BrMemAllocate(sizeof(int) * pRev->nslots * pRev->nslices, BR_MEMORY_APPLICATION);
    slot = 0;
    for (i = 0; i < nv; i++) {
        int v = i;

        if (pRev->slice_of[i] != 0) {
            continue;
        }
        if (slot >= pRev->nslots) {
            return 0;
        }
        for (s = 0; s < pRev->nslices; s++) {
            if (v < 0 || pRev->slice_of[v] != s || pRev->slot_of[v] != -1) {
                return 0;
            }
            // Every vertex of one ring has to sit at the same place on the
            // profile - same distance along the axle, same radius - which is
            // what makes the model a surface of revolution in the first
            // place, and what lets BuildRoundRing place the new ring at a
            // single x.
            if (fabs((double)(pModel->vertices[v].p.v[0] - pModel->vertices[i].p.v[0])) > REV_POS_TOL
                || fabs(pS->radius[v] - pS->radius[i]) > REV_POS_TOL) {
                return 0;
            }
            pRev->slot_of[v] = slot;
            pRev->orbit[slot * pRev->nslices + s] = v;
            v = pS->sigma[v];
        }
        if (v != i) {
            return 0;
        }
        slot++;
    }
    return slot == pRev->nslots;
}

// Added by dethrace
// True if every vertex of a ring carries the same texture coordinates, which
// means they say nothing about how the texture runs around the wheel - see
// tRing_wrap.
static int OrbitUVIsConstant(br_model* pModel, tRev_analysis* pRev, int pSlot) {
    br_vertex* first = &pModel->vertices[pRev->orbit[pSlot * pRev->nslices]];
    int s;

    for (s = 1; s < pRev->nslices; s++) {
        br_vertex* v = &pModel->vertices[pRev->orbit[pSlot * pRev->nslices + s]];

        if (fabs((double)(v->map.v[0] - first->map.v[0])) > REV_UV_TOL
            || fabs((double)(v->map.v[1] - first->map.v[1])) > REV_UV_TOL) {
            return 0;
        }
    }
    return 1;
}

// Added by dethrace
static void SortSlotTriple(int* pSlot, int* pOut) {
    int i, j;

    for (i = 0; i < 3; i++) {
        pOut[i] = pSlot[i];
    }
    for (i = 1; i < 3; i++) {
        int v = pOut[i];

        j = i - 1;
        while (j >= 0 && pOut[j] > v) {
            pOut[j + 1] = pOut[j];
            j--;
        }
        pOut[j + 1] = v;
    }
}

// Added by dethrace
// Whether a face of the repeating period joins the same rings as the given
// one.
//
// Deliberately compares which rings the triangle touches rather than the
// triangle itself: the quads between two rings are not consistently
// triangulated in the shipped models (Roadhog's rear rim splits some on one
// diagonal and some on the other), and which diagonal a quad happens to use
// says nothing about the shape. The regenerated mesh picks one diagonal - the
// first segment's - and uses it all the way round.
static int BandPatternMatches(tRev_band* pBand, int* pSorted_slots) {
    int mine[3];
    int i;

    SortSlotTriple(pBand->slot, mine);
    for (i = 0; i < 3; i++) {
        if (mine[i] != pSorted_slots[i]) {
            return 0;
        }
    }
    return 1;
}

// Added by dethrace
// Sorts every face into either a band (joins two neighbouring slices, so it
// is part of the repeating period) or a cap (closes one ring off), and checks
// that the bands really do repeat identically all the way round. Returns 0 if
// anything doesn't fit.
static int ClassifyRevolutionFaces(br_model* pModel, tRev_build* pB) {
    tRev_analysis* a = &pB->a;
    int nband_total = 0;
    int i, j, s, b;

    for (i = 0; i < pModel->nfaces; i++) {
        br_face* face = &pModel->faces[i];
        int slices[3];
        int slot_seen = -1;
        int npole = 0;
        int pole_v = -1;

        for (j = 0; j < 3; j++) {
            int v = face->vertices[j];

            slices[j] = a->slice_of[v];
            if (slices[j] < 0) {
                npole++;
                pole_v = v;
            } else if (slot_seen == -1) {
                slot_seen = a->slot_of[v];
            } else if (slot_seen != a->slot_of[v]) {
                slot_seen = -2;
            }
        }
        if (npole > 1) {
            return 0;
        }

        if (slot_seen >= 0) {
            // Every non-hub vertex is on one ring, so this face is part of
            // the disc closing that ring off. Checked (below) against the
            // triangle count a disc over this many vertices must have.
            tRev_cap* cap = &pB->cap[slot_seen];
            br_vector3 edge1, edge2, normal;

            if (cap->nfaces == 0) {
                cap->face = *face;
                cap->pole = pole_v;
            } else if ((cap->pole >= 0) != (npole > 0) || (npole > 0 && cap->pole != pole_v)) {
                return 0;
            }
            BrVector3Sub(&edge1, &pModel->vertices[face->vertices[1]].p, &pModel->vertices[face->vertices[0]].p);
            BrVector3Sub(&edge2, &pModel->vertices[face->vertices[2]].p, &pModel->vertices[face->vertices[0]].p);
            BrVector3Cross(&normal, &edge1, &edge2);
            cap->outward += (double)normal.v[0];
            cap->nfaces++;
            pB->face_kind[i] = 1;
            pB->face_key[i] = slot_seen;
            continue;
        }
        if (npole > 0) {
            // A hub vertex joined to more than one ring isn't a cap and
            // isn't a band.
            return 0;
        }

        // Otherwise it must join two neighbouring slices.
        {
            int s0 = slices[0];
            int s1 = -1;
            int base;

            for (j = 1; j < 3; j++) {
                if (slices[j] == s0) {
                    continue;
                }
                if (s1 < 0) {
                    s1 = slices[j];
                } else if (s1 != slices[j]) {
                    return 0;
                }
            }
            if (s1 < 0) {
                return 0;
            }
            if ((s0 + 1) % a->nslices == s1) {
                base = s0;
            } else if ((s1 + 1) % a->nslices == s0) {
                base = s1;
            } else {
                return 0;
            }
            pB->face_kind[i] = 0;
            pB->face_key[i] = base;
            nband_total++;
        }
    }

    for (s = 0; s < a->nslots; s++) {
        int expected;

        if (pB->cap[s].nfaces == 0) {
            continue;
        }
        // A disc fanned from a hub needs one triangle per ring vertex; one
        // triangulated from a ring vertex of its own needs two fewer.
        expected = pB->cap[s].pole >= 0 ? a->nslices : a->nslices - 2;
        if (pB->cap[s].nfaces != expected || pB->cap[s].outward == 0.0) {
            return 0;
        }
    }

    if (nband_total == 0 || nband_total % a->nslices != 0) {
        return 0;
    }
    pB->nbands = nband_total / a->nslices;

    // Take the faces bridging the first two slices as the period.
    b = 0;
    for (i = 0; i < pModel->nfaces; i++) {
        if (pB->face_kind[i] != 0 || pB->face_key[i] != 0) {
            continue;
        }
        if (b >= pB->nbands) {
            return 0;
        }
        pB->band[b].face = pModel->faces[i];
        for (j = 0; j < 3; j++) {
            int v = pModel->faces[i].vertices[j];

            pB->band[b].slot[j] = a->slot_of[v];
            pB->band[b].offset[j] = a->slice_of[v] == 0 ? 0 : 1;
        }
        b++;
    }
    if (b != pB->nbands) {
        return 0;
    }

    // Every other segment must be that same set of faces over again.
    for (s = 1; s < a->nslices; s++) {
        int matched = 0;

        memset(pB->band_used, 0, sizeof(int) * pModel->nfaces);
        for (i = 0; i < pModel->nfaces; i++) {
            int slot[3];
            int sorted[3];

            if (pB->face_kind[i] != 0 || pB->face_key[i] != s) {
                continue;
            }
            for (j = 0; j < 3; j++) {
                slot[j] = a->slot_of[pModel->faces[i].vertices[j]];
            }
            SortSlotTriple(slot, sorted);
            for (b = 0; b < pB->nbands; b++) {
                if (!pB->band_used[b] && BandPatternMatches(&pB->band[b], sorted)) {
                    pB->band_used[b] = 1;
                    matched++;
                    break;
                }
            }
            if (b == pB->nbands) {
                return 0;
            }
        }
        if (matched != pB->nbands) {
            return 0;
        }
    }
    return 1;
}

// Added by dethrace
// Works out which of the period's faces should end up smoothed together.
//
// The originals can't just be copied across: they group by angular segment
// (each facet of the octagon in its own group, so it reads as faceted), and
// replicating that would keep the new mesh looking faceted. What does carry
// over is which *surfaces* of the wheel were creased apart from each other -
// tread against sidewall, say - so faces are grouped here by whether their
// originals shared a smoothing bit, and each resulting group then gets one
// bit of its own, continuous all the way round.
static void AssignBandSmoothingGroups(tRev_build* pB) {
    int b, c;
    int ngroups = 0;

    for (b = 0; b < pB->nbands; b++) {
        pB->band[b].group = b;
    }
    for (b = 0; b < pB->nbands; b++) {
        for (c = b + 1; c < pB->nbands; c++) {
            // prepmesh.c reads a smoothing value of 0 as "smooth with
            // everything", so treat it that way when comparing.
            br_uint_16 sb = pB->band[b].face.smoothing != 0 ? pB->band[b].face.smoothing : 0xffff;
            br_uint_16 sc = pB->band[c].face.smoothing != 0 ? pB->band[c].face.smoothing : 0xffff;
            int old_group;
            int d;

            if ((sb & sc) == 0 || pB->band[b].group == pB->band[c].group) {
                continue;
            }
            old_group = pB->band[c].group;
            for (d = 0; d < pB->nbands; d++) {
                if (pB->band[d].group == old_group) {
                    pB->band[d].group = pB->band[b].group;
                }
            }
        }
    }

    // Renumber to 0,1,2..., then map onto bits 0..14 - bit 15 is reserved
    // for the caps, which must not smooth into any band.
    for (b = 0; b < pB->nbands; b++) {
        if (pB->band[b].group != b) {
            continue;
        }
        for (c = b; c < pB->nbands; c++) {
            if (pB->band[c].group == b) {
                pB->band[c].group = -1 - ngroups;
            }
        }
        ngroups++;
    }
    for (b = 0; b < pB->nbands; b++) {
        pB->band[b].group = -1 - pB->band[b].group;
        if (pB->band[b].group > 14) {
            pB->band[b].group = 14;
        }
    }
}

// Added by dethrace
// Spots rings whose texture wraps around the wheel instead of being projected
// onto it, and records the mapping so BuildRoundRing can keep its density.
//
// The tell is a band face joining two *different* rings that sit at the same
// place on the profile: geometrically that band has no width, so the only
// thing it spans is the texture, from one ring's coordinates to the other's.
// Monster Masher's tyre is built this way, one full copy of the tread texture
// per angular segment, with the ring duplicated at every seam.
static void FindWrappedRings(br_model* pModel, tRev_build* pB) {
    tRev_analysis* a = &pB->a;
    int b, j, k;

    for (b = 0; b < pB->nbands; b++) {
        for (j = 0; j < 3; j++) {
            for (k = 0; k < 3; k++) {
                int slot_a = pB->band[b].slot[j];
                int slot_b = pB->band[b].slot[k];
                br_vertex* va;
                br_vertex* vb;

                if (pB->band[b].offset[j] != 0 || pB->band[b].offset[k] != 1 || slot_a == slot_b) {
                    continue;
                }
                if (pB->has_wrap[slot_a] || pB->has_wrap[slot_b]) {
                    continue;
                }
                va = &pModel->vertices[a->orbit[slot_a * a->nslices]];
                vb = &pModel->vertices[a->orbit[slot_b * a->nslices + 1]];
                if (fabs((double)(va->p.v[0] - vb->p.v[0])) > REV_POS_TOL
                    || fabs(RingRadius(va) - RingRadius(vb)) > REV_POS_TOL) {
                    continue;
                }
                if (fabs((double)(va->map.v[0] - vb->map.v[0])) < REV_UV_TOL
                    && fabs((double)(va->map.v[1] - vb->map.v[1])) < REV_UV_TOL) {
                    continue;
                }
                if (!OrbitUVIsConstant(pModel, a, slot_a) || !OrbitUVIsConstant(pModel, a, slot_b)) {
                    continue;
                }
                // Both rings get the same mapping: the second ring's copy at
                // segment i closes the band the first ring's copy at segment
                // i-1 opened, so they must agree at that point.
                pB->wrap[slot_a].base[0] = va->map.v[0];
                pB->wrap[slot_a].base[1] = va->map.v[1];
                pB->wrap[slot_a].delta[0] = vb->map.v[0] - va->map.v[0];
                pB->wrap[slot_a].delta[1] = vb->map.v[1] - va->map.v[1];
                pB->wrap[slot_b] = pB->wrap[slot_a];
                pB->has_wrap[slot_a] = 1;
                pB->has_wrap[slot_b] = 1;
            }
        }
    }
}

// Added by dethrace
static int BuildRevolutionMeshInner(br_model* pModel, tRev_build* pB, tRev_scratch* pS, br_vertex** pOut_verts,
    int* pOut_nvertices, br_face** pOut_faces, int* pOut_nfaces) {
    tRev_analysis* a = &pB->a;
    const int n = ROUND_WHEEL_SEGMENTS;
    br_vertex* new_verts;
    br_face* new_faces;
    int new_nvertices, new_nfaces;
    int npoles_out = 0;
    int ring_stride;
    int slot, i, j, b, f;

    if (pModel->nvertices < 16 || pModel->nfaces < 16) {
        return 0;
    }
    if (!AnalyseRevolution(pModel, a, pS)) {
        return 0;
    }
    pB->cap = BrMemAllocate(sizeof(tRev_cap) * a->nslots, BR_MEMORY_APPLICATION);
    pB->wrap = BrMemAllocate(sizeof(tRing_wrap) * a->nslots, BR_MEMORY_APPLICATION);
    pB->has_wrap = BrMemAllocate(sizeof(int) * a->nslots, BR_MEMORY_APPLICATION);
    memset(pB->cap, 0, sizeof(tRev_cap) * a->nslots);
    memset(pB->wrap, 0, sizeof(tRing_wrap) * a->nslots);
    memset(pB->has_wrap, 0, sizeof(int) * a->nslots);
    for (slot = 0; slot < a->nslots; slot++) {
        pB->cap[slot].pole = -1;
    }

    if (!ClassifyRevolutionFaces(pModel, pB)) {
        return 0;
    }
    AssignBandSmoothingGroups(pB);
    FindWrappedRings(pModel, pB);

    // Each ring gets one extra vertex closing the loop, sitting exactly on top
    // of its first but carrying the texture coordinates the mapping has reached
    // after a full turn. Without it a wrapped mapping - which advances by a
    // whole texture repeat per revolution - has nowhere to put that advance,
    // and the one face joining the last vertex back to the first has to sweep
    // the entire texture backwards. The original meshes carry the same seam
    // duplicates for the same reason (Hammer's v16/v17, Monster's duplicate
    // rings); collapsing the ring to a bare loop threw them away.
    ring_stride = n + 1;
    new_nvertices = a->nslots * ring_stride + a->npoles;
    new_nfaces = pB->nbands * n;
    for (slot = 0; slot < a->nslots; slot++) {
        if (pB->cap[slot].nfaces != 0) {
            new_nfaces += pB->cap[slot].pole >= 0 ? n : n - 2;
        }
    }
    if (new_nvertices > 65000 || new_nfaces > 65000) {
        return 0;
    }

    new_verts = BrResAllocate(pModel, sizeof(br_vertex) * new_nvertices, BR_MEMORY_VERTICES);
    new_faces = BrResAllocate(pModel, sizeof(br_face) * new_nfaces, BR_MEMORY_FACES);
    memset(new_verts, 0, sizeof(br_vertex) * new_nvertices);
    memset(new_faces, 0, sizeof(br_face) * new_nfaces);

    for (slot = 0; slot < a->nslots; slot++) {
        br_vertex samples[MAX_ORIG_SEGMENTS];

        for (i = 0; i < a->nslices; i++) {
            samples[i] = pModel->vertices[a->orbit[slot * a->nslices + i]];
        }
        BuildRoundRing(samples, a->nslices, samples[0].p.v[0], pB->has_wrap[slot] ? &pB->wrap[slot] : NULL,
            ring_stride, &new_verts[slot * ring_stride]);
    }
    // Hub vertices are copied verbatim, x included: several wheels recess
    // theirs inwards and that offset is exactly what dishes the wheel face.
    for (i = 0; i < pModel->nvertices; i++) {
        if (a->slice_of[i] >= 0) {
            continue;
        }
        pB->pole_out[i] = a->nslots * ring_stride + npoles_out;
        new_verts[pB->pole_out[i]] = pModel->vertices[i];
        npoles_out++;
    }

    f = 0;
    for (i = 0; i < n; i++) {
        for (b = 0; b < pB->nbands; b++) {
            // Carries the original material, prelit colours and flags over.
            new_faces[f] = pB->band[b].face;
            for (j = 0; j < 3; j++) {
                // No wrap on the segment index: i + offset reaches n at the
                // last segment, which is the closing vertex added above.
                new_faces[f].vertices[j]
                    = (br_uint_16)(pB->band[b].slot[j] * ring_stride + i + pB->band[b].offset[j]);
            }
            new_faces[f].smoothing = (br_uint_16)(1 << pB->band[b].group);
            f++;
        }
    }
    for (slot = 0; slot < a->nslots; slot++) {
        if (pB->cap[slot].nfaces == 0) {
            continue;
        }
        if (pB->cap[slot].pole >= 0) {
            AppendCapFan(new_faces, &f, pB->pole_out[pB->cap[slot].pole], slot * ring_stride, n, 0, n, new_verts,
                (br_scalar)pB->cap[slot].outward, 0x8000, &pB->cap[slot].face);
        } else {
            AppendCapFan(new_faces, &f, slot * ring_stride, slot * ring_stride, n, 1, n - 2, new_verts,
                (br_scalar)pB->cap[slot].outward, 0x8000, &pB->cap[slot].face);
        }
    }

    *pOut_verts = new_verts;
    *pOut_nvertices = new_nvertices;
    *pOut_faces = new_faces;
    *pOut_nfaces = new_nfaces;
    return 1;
}

// Added by dethrace
// Allocates the working state for the general path, runs it, and frees up
// afterwards either way, so everything above can just return 0 to give up.
static int BuildRevolutionMesh(br_model* pModel, br_vertex** pOut_verts, int* pOut_nvertices, br_face** pOut_faces,
    int* pOut_nfaces) {
    tRev_build build;
    tRev_scratch scratch;
    int nv = pModel->nvertices;
    int nf = pModel->nfaces;
    int result;

    memset(&build, 0, sizeof(build));
    build.a.slice_of = BrMemAllocate(sizeof(int) * nv, BR_MEMORY_APPLICATION);
    build.a.slot_of = BrMemAllocate(sizeof(int) * nv, BR_MEMORY_APPLICATION);
    build.face_kind = BrMemAllocate(sizeof(int) * nf, BR_MEMORY_APPLICATION);
    build.face_key = BrMemAllocate(sizeof(int) * nf, BR_MEMORY_APPLICATION);
    build.band = BrMemAllocate(sizeof(tRev_band) * nf, BR_MEMORY_APPLICATION);
    build.band_used = BrMemAllocate(sizeof(int) * nf, BR_MEMORY_APPLICATION);
    build.pole_out = BrMemAllocate(sizeof(int) * nv, BR_MEMORY_APPLICATION);
    scratch.angle = BrMemAllocate(sizeof(double) * nv, BR_MEMORY_APPLICATION);
    scratch.radius = BrMemAllocate(sizeof(double) * nv, BR_MEMORY_APPLICATION);
    scratch.order = BrMemAllocate(sizeof(int) * nv, BR_MEMORY_APPLICATION);
    scratch.sigma = BrMemAllocate(sizeof(int) * nv, BR_MEMORY_APPLICATION);
    scratch.claimed = BrMemAllocate(sizeof(int) * nv, BR_MEMORY_APPLICATION);
    scratch.materials = BrMemAllocate(sizeof(br_material*) * nv * MAX_VERTEX_MATERIALS, BR_MEMORY_APPLICATION);
    scratch.material_count = BrMemAllocate(sizeof(int) * nv, BR_MEMORY_APPLICATION);
    BuildVertexMaterialSets(pModel, scratch.materials, scratch.material_count);

    result = BuildRevolutionMeshInner(pModel, &build, &scratch, pOut_verts, pOut_nvertices, pOut_faces, pOut_nfaces);

    BrMemFree(scratch.material_count);
    BrMemFree(scratch.materials);
    BrMemFree(scratch.claimed);
    BrMemFree(scratch.sigma);
    BrMemFree(scratch.order);
    BrMemFree(scratch.radius);
    BrMemFree(scratch.angle);
    BrMemFree(build.pole_out);
    BrMemFree(build.band_used);
    BrMemFree(build.band);
    BrMemFree(build.face_key);
    BrMemFree(build.face_kind);
    BrMemFree(build.a.slot_of);
    BrMemFree(build.a.slice_of);
    if (build.a.orbit != NULL) {
        BrMemFree(build.a.orbit);
    }
    if (build.cap != NULL) {
        BrMemFree(build.cap);
        BrMemFree(build.wrap);
        BrMemFree(build.has_wrap);
    }
    return result;
}

// Added by dethrace
// Collapses vertices that don't need to be separate.
//
// Always merges exact duplicates - identical position, mapping and prelit
// colour, so nothing can render differently afterwards. Splat Pack ships a
// 74-vertex BUFWHEEL.DAT whose geometry is the ordinary 16-vertex octagon with
// most vertices duplicated up to eight times, an exporter artefact, and
// collapsing those turns it back into a shape the generators recognize.
//
// With pMerge_seams set it additionally merges vertices that stand in the same
// place and belong to the same surface of the wheel (same set of face
// materials) but carry different texture coordinates. That is a texture seam:
// the model holds two copies of one corner so the mapping can run off one edge
// of the texture and back on at the other. Hammer's HMRWHEEL.DAT has two, and
// they are what stop its slices holding equal numbers of vertices. Dropping
// one copy's coordinates is safe here because every ring's mapping is
// regenerated from its own samples afterwards, and LerpWrapped already takes
// the short way round the seam - the duplicate carries no information the
// generators need. It is a second-chance measure all the same, applied only to
// models nothing else could make sense of.
//
// Returns 1 if it installed new arrays on the model. The original arrays are
// left untouched for the caller to keep or free once it knows whether anything
// came of it.
static int ReduceModelVertices(br_model* pModel, int pMerge_seams) {
    int* remap;
    int* kept;
    br_material** materials = NULL;
    int* material_count = NULL;
    br_vertex* new_verts;
    br_face* new_faces;
    int new_nvertices = 0;
    int i, j;

    remap = BrMemAllocate(sizeof(int) * pModel->nvertices, BR_MEMORY_APPLICATION);
    kept = BrMemAllocate(sizeof(int) * pModel->nvertices, BR_MEMORY_APPLICATION);
    new_verts = BrResAllocate(pModel, sizeof(br_vertex) * pModel->nvertices, BR_MEMORY_VERTICES);
    if (pMerge_seams) {
        materials
            = BrMemAllocate(sizeof(br_material*) * pModel->nvertices * MAX_VERTEX_MATERIALS, BR_MEMORY_APPLICATION);
        material_count = BrMemAllocate(sizeof(int) * pModel->nvertices, BR_MEMORY_APPLICATION);
        BuildVertexMaterialSets(pModel, materials, material_count);
    }
    for (i = 0; i < pModel->nvertices; i++) {
        br_vertex* v = &pModel->vertices[i];

        remap[i] = -1;
        for (j = 0; j < new_nvertices; j++) {
            br_vertex* w = &new_verts[j];

            if (v->p.v[0] == w->p.v[0] && v->p.v[1] == w->p.v[1] && v->p.v[2] == w->p.v[2]
                && v->map.v[0] == w->map.v[0] && v->map.v[1] == w->map.v[1] && v->index == w->index
                && v->red == w->red && v->grn == w->grn && v->blu == w->blu) {
                remap[i] = j;
                break;
            }
            if (pMerge_seams && fabs((double)(v->p.v[0] - w->p.v[0])) < REV_POS_TOL
                && fabs((double)(v->p.v[1] - w->p.v[1])) < REV_POS_TOL
                && fabs((double)(v->p.v[2] - w->p.v[2])) < REV_POS_TOL
                && VertexMaterialSetsMatch(materials, material_count, i, kept[j])) {
                remap[i] = j;
                break;
            }
        }
        if (remap[i] < 0) {
            remap[i] = new_nvertices;
            kept[new_nvertices] = i;
            new_verts[new_nvertices] = *v;
            new_nvertices++;
        }
    }
    if (materials != NULL) {
        BrMemFree(material_count);
        BrMemFree(materials);
    }
    BrMemFree(kept);
    if (new_nvertices == pModel->nvertices) {
        BrResFree(new_verts);
        BrMemFree(remap);
        return 0;
    }

    new_faces = BrResAllocate(pModel, sizeof(br_face) * pModel->nfaces, BR_MEMORY_FACES);
    for (i = 0; i < pModel->nfaces; i++) {
        new_faces[i] = pModel->faces[i];
        for (j = 0; j < 3; j++) {
            new_faces[i].vertices[j] = (br_uint_16)remap[pModel->faces[i].vertices[j]];
        }
    }
    BrMemFree(remap);

    pModel->vertices = new_verts;
    pModel->nvertices = (br_uint_16)new_nvertices;
    pModel->faces = new_faces;
    return 1;
}

// Added by dethrace
// Replaces a low-poly wheel model with a rounder one, in place, trying the
// two-ring cylinder generator first and the general surface-of-revolution one
// after it. Leaves the model exactly as found if neither recognizes it.
//
// Models are shared by identifier across cars, so this runs once per car that
// references a given model and has to be idempotent - see the notes on
// BuildTwoRingMesh and the segment-count check in AnalyseRevolution.
void RoundOffWheelModel(br_model* pModel) {
    br_vertex* saved_verts;
    br_face* saved_faces;
    br_uint_16 saved_nvertices;
    br_uint_16 saved_nfaces;
    br_vertex* reduced_verts = NULL;
    br_face* reduced_faces = NULL;
    br_vertex* new_verts = NULL;
    br_face* new_faces = NULL;
    int new_nvertices = 0;
    int new_nfaces = 0;
    int built = 0;
    int attempt;

    if (pModel == NULL || pModel->vertices == NULL || pModel->faces == NULL || pModel->nvertices < 16
        || pModel->nfaces < 16) {
        return;
    }

    saved_verts = pModel->vertices;
    saved_faces = pModel->faces;
    saved_nvertices = pModel->nvertices;
    saved_nfaces = pModel->nfaces;

    for (attempt = 0; attempt < 2 && !built; attempt++) {
        // Start each attempt from exactly what was loaded.
        if (reduced_verts != NULL) {
            BrResFree(reduced_verts);
            BrResFree(reduced_faces);
            reduced_verts = NULL;
            reduced_faces = NULL;
        }
        pModel->vertices = saved_verts;
        pModel->nvertices = saved_nvertices;
        pModel->faces = saved_faces;
        pModel->nfaces = saved_nfaces;

        if (ReduceModelVertices(pModel, attempt == 1)) {
            reduced_verts = pModel->vertices;
            reduced_faces = pModel->faces;
        }
        if (attempt == 0) {
            // The two-ring generator goes first: it is what all the ordinary
            // cylinder wheels already go through, and it alone keeps
            // decorative geometry welded onto the wheel.
            built = BuildTwoRingMesh(pModel, &new_verts, &new_nvertices, &new_faces, &new_nfaces)
                || BuildRevolutionMesh(pModel, &new_verts, &new_nvertices, &new_faces, &new_nfaces);
        } else {
            // Merging seam duplicates leaves extra copies of a ring sitting
            // exactly on top of the ring itself, which the two-ring generator
            // would write off as stray geometry - losing the material and
            // mapping they carry. The general generator reads them as the
            // separate rings they are, so it gets first refusal here.
            built = BuildRevolutionMesh(pModel, &new_verts, &new_nvertices, &new_faces, &new_nfaces)
                || BuildTwoRingMesh(pModel, &new_verts, &new_nvertices, &new_faces, &new_nfaces);
        }
    }

    if (!built) {
        // A shape no generator understands (or one already rounded off by an
        // earlier car referencing the same model). Put back exactly what was
        // there rather than leave it half-processed.
        if (reduced_verts != NULL) {
            BrResFree(reduced_verts);
            BrResFree(reduced_faces);
        }
        pModel->vertices = saved_verts;
        pModel->nvertices = saved_nvertices;
        pModel->faces = saved_faces;
        pModel->nfaces = saved_nfaces;
        return;
    }

    if (reduced_verts != NULL) {
        BrResFree(reduced_verts);
        BrResFree(reduced_faces);
    }
    BrResFree(saved_verts);
    BrResFree(saved_faces);
    pModel->vertices = new_verts;
    pModel->nvertices = (br_uint_16)new_nvertices;
    pModel->faces = new_faces;
    pModel->nfaces = (br_uint_16)new_nfaces;

    // If this model was authored with custom per-vertex normals
    // (BR_MODF_CUSTOM_NORMALS), BrModelUpdate uses whatever is in each
    // vertex's .n field directly as its lighting normal instead of computing
    // one from face geometry + smoothing groups - and we never set .n on the
    // new vertices (it's not something we can sensibly copy or interpolate
    // from the original 8-gon). Left set, a material using per-vertex dynamic
    // lighting would compute zero light contribution from that zeroed normal
    // and render solid black regardless of correct UVs/texture. Clearing it
    // makes BrModelUpdate derive normals from our actual geometry and
    // smoothing groups instead.
    pModel->flags &= ~BR_MODF_CUSTOM_NORMALS;
    // Likewise BR_MODF_CUSTOM_EQUATIONS makes BrModelUpdate trust each face's
    // stored plane equation (br_face::n / ::d). The regenerated faces inherit
    // those fields from whichever original face they were seeded from, so
    // they describe the wrong plane; clear the flag so the equations get
    // recomputed from the new geometry.
    pModel->flags &= ~BR_MODF_CUSTOM_EQUATIONS;
    BrModelUpdate(pModel, BR_MODU_ALL);
}

// ---------------------------------------------------------------------------
// Mirroring the wheels on one side of the car.
//
// The engine draws both wheels of a pair from one model in one orientation:
// the left and right wheel actors differ by a translation and nothing else
// (identity rotation on both, verified with `wheeldump <car.ACT> <actor>`).
// That is fine for the usual wheel, which is symmetric about its axle, but a
// wheel dished on one side only then has its dish pointing outwards on one
// side of the car and inwards on the other. Helga's rear wheels do exactly
// that, in the original game as much as here - it is simply easier to see once
// the wheel is round.
//
// So where a left/right pair shares one model and that model is asymmetric,
// the left actor gets a mirrored copy of its own, built at load time and owned
// by the car's storage space. Nothing is written to disk.
// ---------------------------------------------------------------------------

// Added by dethrace
// True if mirroring along the axle would leave the model unchanged, i.e. every
// vertex has a partner directly opposite it. Most wheels are built this way,
// and mirroring one would be pointless work - and would flip the handedness of
// its texture for nothing.
br_scalar WheelModelAxleCentre(br_model* pModel) {
    br_scalar lo, hi;
    int i;

    lo = hi = pModel->vertices[0].p.v[0];
    for (i = 1; i < pModel->nvertices; i++) {
        if (pModel->vertices[i].p.v[0] < lo) {
            lo = pModel->vertices[i].p.v[0];
        }
        if (pModel->vertices[i].p.v[0] > hi) {
            hi = pModel->vertices[i].p.v[0];
        }
    }
    return (lo + hi) * 0.5f;
}

double WheelModelAxleAsymmetry(br_model* pModel) {
    // Measured about the wheel's own centre plane, not x=0. A wheel need not
    // straddle the origin - the Blood Mobile's (AMFWHL.DAT) is a perfectly
    // symmetric cylinder whose rings sit at x=-0.0429 and x=+0.0629, i.e.
    // seated 0.01 along the axle from where its local origin is. Measuring
    // about x=0 reports that seating offset as asymmetry, and mirroring on the
    // strength of it would shove the wheel sideways relative to the bodywork
    // for no visual gain, since there is no dish to correct.
    double centre = (double)WheelModelAxleCentre(pModel);
    double worst = 0.0;
    int i, j;

    for (i = 0; i < pModel->nvertices; i++) {
        double best = 1e30;
        double mirrored_x = 2.0 * centre - (double)pModel->vertices[i].p.v[0];

        for (j = 0; j < pModel->nvertices; j++) {
            double dx = mirrored_x - (double)pModel->vertices[j].p.v[0];
            double dy = (double)(pModel->vertices[i].p.v[1] - pModel->vertices[j].p.v[1]);
            double dz = (double)(pModel->vertices[i].p.v[2] - pModel->vertices[j].p.v[2]);
            double d = sqrt(dx * dx + dy * dy + dz * dz);

            if (d < best) {
                best = d;
            }
        }
        if (best > worst) {
            worst = best;
        }
    }
    return worst;
}

int WheelModelIsAxleSymmetric(br_model* pModel) {
    return WheelModelAxleAsymmetry(pModel) < MIRROR_ASYMMETRY_MIN;
}

// Added by dethrace
// Which way along the axle the wheel's dished/detailed face points: +1 for the
// +x end, -1 for the -x end, 0 if it is symmetric and has no such face.
//
// The two shipped asymmetric wheels dish in opposite directions, so this can't
// be assumed. Helga's rear (RDRWHL.DAT) carries its recessed rim on the +x
// side; Hawk's (HAWKWHL.DAT) is a plain cylinder except for a hub vertex pulled
// inwards from the -x ring, dishing that end instead.
//
// Found by asking which vertices have no counterpart in the mirrored model -
// those are exactly the geometry that makes one face different from the other -
// and totalling which side of the centre plane they sit on.
int WheelModelDetailSide(br_model* pModel) {
    double centre = (double)WheelModelAxleCentre(pModel);
    double lean = 0.0;
    int i, j;

    for (i = 0; i < pModel->nvertices; i++) {
        double best = 1e30;
        double mirrored_x = 2.0 * centre - (double)pModel->vertices[i].p.v[0];

        for (j = 0; j < pModel->nvertices; j++) {
            double dx = mirrored_x - (double)pModel->vertices[j].p.v[0];
            double dy = (double)(pModel->vertices[i].p.v[1] - pModel->vertices[j].p.v[1]);
            double dz = (double)(pModel->vertices[i].p.v[2] - pModel->vertices[j].p.v[2]);
            double d = sqrt(dx * dx + dy * dy + dz * dz);

            if (d < best) {
                best = d;
            }
        }
        if (best > REV_POS_TOL) {
            lean += (double)pModel->vertices[i].p.v[0] - centre;
        }
    }
    if (lean > 0.0) {
        return 1;
    }
    if (lean < 0.0) {
        return -1;
    }
    return 0;
}

// Added by dethrace
// Builds a mirror image of a wheel model along the axle.
//
// This matches how the game's own artists did it in the one place they
// bothered: Splat Pack Vlad ships VDRRWHL.DAT alongside Vdrwhl.dat, and it is
// the same model with x negated - y, z and the texture coordinates all left
// exactly as they were. Negating one axis reverses the handedness of every
// triangle, so the winding is flipped to match, or the wheel renders inside
// out.
br_model* BuildMirroredWheelModel(br_model* pSource, char* pName) {
    br_model* mirrored;
    // Reflect about the wheel's own centre plane rather than x=0, so a wheel
    // that isn't centred on its local origin keeps its seating along the axle
    // and only changes which way it faces. For a centred wheel the two are the
    // same thing, which is why this still reproduces the game's own mirrored
    // pair (Vlad's Vdrwhl.dat / VDRRWHL.DAT) exactly.
    br_scalar centre = WheelModelAxleCentre(pSource);
    int i;

    mirrored = BrModelAllocate(pName, pSource->nvertices, pSource->nfaces);
    if (mirrored == NULL) {
        return NULL;
    }
    for (i = 0; i < pSource->nvertices; i++) {
        mirrored->vertices[i] = pSource->vertices[i];
        mirrored->vertices[i].p.v[0] = 2.0f * centre - mirrored->vertices[i].p.v[0];
    }
    for (i = 0; i < pSource->nfaces; i++) {
        mirrored->faces[i] = pSource->faces[i];
        mirrored->faces[i].vertices[1] = pSource->faces[i].vertices[2];
        mirrored->faces[i].vertices[2] = pSource->faces[i].vertices[1];
    }
    mirrored->pivot = pSource->pivot;
    mirrored->pivot.v[0] = 2.0f * centre - mirrored->pivot.v[0];
    // Normals and plane equations belong to the geometry we just reflected,
    // so let BrModelUpdate (via BrModelAdd) work them out again.
    mirrored->flags = pSource->flags & ~(BR_MODF_CUSTOM_NORMALS | BR_MODF_CUSTOM_EQUATIONS);
    return mirrored;
}

// Added by dethrace
static br_model* FindStorageModel(tBrender_storage* pStorage_space, char* pName) {
    int i;

    for (i = 0; i < pStorage_space->models_count; i++) {
        if (pStorage_space->models[i] != NULL && pStorage_space->models[i]->identifier != NULL
            && strcmp(pStorage_space->models[i]->identifier, pName) == 0) {
            return pStorage_space->models[i];
        }
    }
    return NULL;
}

// Added by dethrace
// Added by dethrace
// Where the actor sits along the axle relative to the car, accumulated up the
// parent chain. A wheel actor doesn't necessarily carry the left/right offset
// itself - Hawk's front wheels sit at the origin of their own transform and
// hang off a steering pivot that holds the offset - so reading only the wheel
// actor's own translation reports both sides at zero.
static br_scalar WheelActorAxlePosition(br_actor* pActor) {
    br_scalar x = 0.0f;
    br_actor* a;
    int guard = 0;

    for (a = pActor; a != NULL && guard < 32; a = a->parent, guard++) {
        x += a->t.t.translate.t.v[0];
    }
    return x;
}

static void MirrorWheelActorsToFaceOutboard(tCar_spec* pCar_spec, tBrender_storage* pStorage_space) {
    int i;

    if (pStorage_space == NULL) {
        return;
    }
    // gWheel_actor_names pairs each left wheel with the right one after it
    // (FLWHEEL/FRWHEEL, RLWHEEL/RRWHEEL, IFLWHEEL/IFRWHEEL).
    for (i = 0; i + 1 < COUNT_OF(gWheel_actor_names); i += 2) {
        br_actor* left = pCar_spec->wheel_actors[i];
        br_actor* right = pCar_spec->wheel_actors[i + 1];
        br_actor* wrong_side;
        char name[256];
        br_model* mirrored;
        int detail_side;
        br_scalar left_x, right_x;

        if (left == NULL || right == NULL || left->model == NULL) {
            continue;
        }
        // Only when both sides come off the same model. Where the artists
        // supplied one per side (Splat Pack Vlad's rear wheels: Vdrwhl.dat and
        // VDRRWHL.DAT, already mirror images of each other) the car is correct
        // as it stands and mirroring would undo their work.
        if (left->model != right->model || left->model->identifier == NULL) {
            continue;
        }
        if (WheelModelIsAxleSymmetric(left->model)) {
            continue;
        }
        detail_side = WheelModelDetailSide(left->model);
        if (detail_side == 0) {
            continue;
        }

        // The dished face belongs on the outside of the car on both sides, so
        // mirror whichever wheel currently has it pointing inboard - which is
        // not always the left one. The two shipped asymmetric wheels dish
        // opposite ways, so Helga needs her left wheel mirrored and Hawk its
        // right. Outboard is read from where the game actually places the two
        // actors rather than assumed from their names.
        left_x = WheelActorAxlePosition(left);
        right_x = WheelActorAxlePosition(right);
        if (left_x == right_x) {
            // Nothing to tell the sides apart by position; fall back to the
            // naming convention, which holds everywhere in the shipped data:
            // the L actor of each pair is the one at negative x.
            left_x = -1.0f;
            right_x = 1.0f;
        }
        // The wheel sitting at the more negative x has its outside facing -x,
        // so it is wrong when the detail points +x, and vice versa.
        if (detail_side > 0) {
            wrong_side = (left_x < right_x) ? left : right;
        } else {
            wrong_side = (left_x < right_x) ? right : left;
        }

        if (strlen(wrong_side->model->identifier) + 2 >= sizeof(name)) {
            continue;
        }
        sprintf(name, "%s~M", wrong_side->model->identifier);

        // One car can drive several wheels off the same model, and one storage
        // space holds every opponent in a race, so reuse a copy already made.
        mirrored = FindStorageModel(pStorage_space, name);
        if (mirrored == NULL) {
            mirrored = BuildMirroredWheelModel(wrong_side->model, name);
            if (mirrored == NULL) {
                continue;
            }
            BrModelAdd(mirrored);
            if (AddModelToStorage(pStorage_space, mirrored) != eStorage_allocated) {
                // Nothing owns it, so don't leave it behind.
                BrModelRemove(mirrored);
                BrModelFree(mirrored);
                continue;
            }
        }
        wrong_side->model = mirrored;
    }
}

// Added by dethrace
void RoundOffWheelActors(tCar_spec* pCar_spec, tBrender_storage* pStorage_space) {
    int i;

    if (!harness_game_config.round_wheels) {
        return;
    }
    for (i = 0; i < COUNT_OF(gWheel_actor_names); i++) {
        if (pCar_spec->wheel_actors[i] != NULL) {
            RoundOffWheelModel(pCar_spec->wheel_actors[i]->model);
        }
    }
    MirrorWheelActorsToFaceOutboard(pCar_spec, pStorage_space);
}
