#include "skidmark.h"
#include "brender.h"
#include "globvars.h"
#include "globvrbm.h"
#include "harness/config.h"
#include "harness/trace.h"
#include "loading.h"
#include "oil.h"
#include "piping.h"
#include "utility.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// GLOBAL: CARM95 0x00530190
tSkid* gSkids;

// Added by dethrace
// One byte per mark: is this slot holding a mark that has been laid down?
// The original uses render_style == BR_RSTYLE_NONE for "slot is free", but a
// batched mark is not rendered through its own actor, so render_style no longer
// answers the question.
static unsigned char* gSkid_in_use;

// Added by dethrace
// Marks currently laid down. Reported by the frame profiler.
int gSkids_drawn;
int gSkid_batches_live;

// Added by dethrace
// Skid mark batching.
//
// The original gives every mark its own actor and its own two-triangle model,
// all parented to gNon_track_actor. BRender charges per actor: render.c resolves
// the model and material and concatenates the transform before BrOnScreenCheck
// gets to reject it. With the ring raised so marks last a race, that is
// thousands of transforms a frame and the frame rate decays as they build up.
//
// Marks are baked instead, in world space, into shared models of
// SKID_BATCH_QUADS each. Mark n always lives in batch n / SKID_BATCH_QUADS, and
// slots come from one ring counter shared by every car and wheel (the static in
// SkidSection), so a batch holds marks laid at the same time, which are close
// together - keeping its bounding box tight so whole batches cull at once.
//
// Batch count is fixed at gSkid_count / SKID_BATCH_QUADS. That bound matters: the
// renderer defers these primitives into a sorted order table (gstored.c), so
// what costs is the number of quads submitted, and it must not be allowed to
// grow without limit.
//
// Materials do not split batches. br_face carries its own material, so a batch
// spanning tyre marks, blood and oil costs one draw per material present rather
// than one per mark. It does mean layering between materials follows BRender's
// depth sort rather than the order marks were laid - the same as the original,
// which has no say in it either.
//
// This only works because EnsureGroundDetailVisible under DETHRACE_FIX_BUGS
// leaves a mark at its stored ground position; the original slides marks toward
// the camera every frame, which would mean rebuilding every batch every frame.
#define SKID_BATCH_QUADS 256

typedef struct tSkid_batch {
    br_actor* actor;
    br_model* model;
    // Geometry changed since the last BrModelUpdate.
    int dirty;
    // Holds at least one mark, so it is worth submitting.
    int occupied;
    // Marks in it, for the profiler gauge and to know when it empties.
    int live;
} tSkid_batch;

static tSkid_batch* gSkid_batches;
static int gSkid_batch_count;
static int gSkid_batching;
// Added by dethrace
// Upstream is a fixed array, tSkid gSkids[100]. Batching is what makes a
// bigger ring affordable, so the count is derived from it rather than being
// a setting of its own -- two knobs that only ever made sense in one
// combination are one knob.
#define SKID_RING_CLASSIC 100
#define SKID_RING_PERSISTENT 65535
static int gSkid_count;

// Added by dethrace
// The two texture coordinates StretchMark computes for each mark, kept per slot
// rather than in the mark's own model. The per-mark models are all one shared
// square when batching, so there is nowhere else to put them - and a slot needs
// to keep its own, because AdjustSkid (action replay) restores a mark's matrix
// and material without recomputing them, exactly as the original does.
static br_scalar* gSkid_u0;
static br_scalar* gSkid_u1;

// GLOBAL: CARM95 0x00507030
char* gBoring_material_names[2] = { "OILSMEAR.MAT", "ROBSMEAR.MAT" };

// GLOBAL: CARM95 0x00507038
char* gMaterial_names[2] = { "OILSMEAR.MAT", "GIBSMEAR.MAT" };

// Added by dethrace
// Copies one mark's four corners into its batch, transformed into world space.
// The per-mark model keeps its unit square and texture coordinates; only the
// matrix places it, so the bake is matrix * unit square.
static void BakeSkidIntoBatch(int pSkid_num) {
    tSkid_batch* batch;
    br_model* src;
    br_matrix34* mat;
    int quad;
    int v0;
    int i;

    if (!gSkid_batching) {
        return;
    }
    batch = &gSkid_batches[pSkid_num / SKID_BATCH_QUADS];
    quad = pSkid_num % SKID_BATCH_QUADS;
    v0 = quad * 4;
    src = gSkids[pSkid_num].actor->model;
    mat = &gSkids[pSkid_num].actor->t.t.mat;

    if (!batch->occupied) {
        // Collapse every other quad onto this mark rather than leaving them at
        // the origin, which would stretch the bounding box across the map and
        // stop the batch ever being culled.
        batch->occupied = 1;
        for (i = 0; i < SKID_BATCH_QUADS * 4; i++) {
            BrMatrix34ApplyP(&batch->model->vertices[i].p, &src->vertices[0].p, mat);
        }
    }
    if (!gSkid_in_use[pSkid_num]) {
        gSkid_in_use[pSkid_num] = 1;
        batch->live++;
        gSkids_drawn++;
    }

    for (i = 0; i < 4; i++) {
        BrMatrix34ApplyP(&batch->model->vertices[v0 + i].p, &src->vertices[i].p, mat);
    }
    // Corner order matches the shared square: 0 and 1 at the start of the mark,
    // 2 and 3 at its end, V across the width.
    BrVector2Set(&batch->model->vertices[v0 + 0].map, gSkid_u0[pSkid_num], 0.0f);
    BrVector2Set(&batch->model->vertices[v0 + 1].map, gSkid_u0[pSkid_num], 1.0f);
    BrVector2Set(&batch->model->vertices[v0 + 2].map, gSkid_u1[pSkid_num], 1.0f);
    BrVector2Set(&batch->model->vertices[v0 + 3].map, gSkid_u1[pSkid_num], 0.0f);
    batch->model->faces[quad * 2 + 0].material = gSkids[pSkid_num].actor->material;
    batch->model->faces[quad * 2 + 1].material = gSkids[pSkid_num].actor->material;
    batch->dirty = 1;
}

// Added by dethrace
// Collapses a mark to a point so it rasterises to nothing, leaving the rest of
// its batch alone.
static void ClearSkidFromBatch(int pSkid_num) {
    tSkid_batch* batch;
    int v0;
    int i;

    if (!gSkid_batching || gSkid_batches == NULL) {
        return;
    }
    batch = &gSkid_batches[pSkid_num / SKID_BATCH_QUADS];
    if (!batch->occupied || !gSkid_in_use[pSkid_num]) {
        return;
    }
    gSkid_in_use[pSkid_num] = 0;
    batch->live--;
    gSkids_drawn--;
    v0 = (pSkid_num % SKID_BATCH_QUADS) * 4;
    for (i = 1; i < 4; i++) {
        batch->model->vertices[v0 + i].p = batch->model->vertices[v0].p;
    }
    batch->dirty = 1;

    if (batch->live == 0) {
        // Emptied, so stop submitting it and let the next mark re-seed its
        // quads. Without this the collapsed quads keep the previous race's
        // positions (HideSkids runs from InitRace) and the first mark of the
        // new race would give the batch a bounding box spanning both tracks.
        batch->occupied = 0;
        batch->actor->render_style = BR_RSTYLE_NONE;
    }
}

// IDA: void __usercall AdjustSkid(int pSkid_num@<EAX>, br_matrix34 *pMatrix@<EDX>, int pMaterial_index@<EBX>)
// FUNCTION: CARM95 0x00401000
void AdjustSkid(int pSkid_num, br_matrix34* pMatrix, int pMaterial_index) {
    memcpy(&gSkids[pSkid_num].actor->t.t.mat, pMatrix, sizeof(br_matrix34));
    memcpy(&gSkids[pSkid_num].pos, &pMatrix->m[3][0], sizeof(br_vector3));
    gSkids[pSkid_num].actor->material = MaterialFromIndex(pMaterial_index);
    gSkids[pSkid_num].actor->render_style = BR_RSTYLE_DEFAULT;
    // Added by dethrace
    BakeSkidIntoBatch(pSkid_num);
}

// IDA: br_material* __usercall MaterialFromIndex@<EAX>(int pIndex@<EAX>)
// FUNCTION: CARM95 0x00401088
br_material* MaterialFromIndex(int pIndex) {
    if (pIndex <= -2) {
        return gMaterial[-2 - pIndex];
    } else {
        return gCurrent_race.material_modifiers[pIndex].skid_mark_material;
    }
}

// IDA: void __cdecl InitSkids()
// FUNCTION: CARM95 0x004010c8
void InitSkids(void) {
    int skid;
    int mat;
    int sl;
    br_model* square;
    char* str;
#if defined(DETHRACE_FIX_BUGS)
    char mat_name[32];
#endif

    for (mat = 0; mat < COUNT_OF(gMaterial_names); mat++) {
        gMaterial[mat] = BrMaterialFind(gProgram_state.sausage_eater_mode ? gBoring_material_names[mat] : gMaterial_names[mat]);

        if (gMaterial[mat] == NULL) {

#if defined(DETHRACE_FIX_BUGS)
            // Avoid modification of read-only data by strtok.
            strcpy(mat_name, gProgram_state.sausage_eater_mode ? gBoring_material_names[mat] : gMaterial_names[mat]);
            str = strtok(mat_name, ".");
#else
            str = strtok(gProgram_state.sausage_eater_mode ? gBoring_material_names[mat] : gMaterial_names[mat], ".");
#endif

            sl = strlen(str);
            strcat(str, ".PIX");
            BrMapAdd(LoadPixelmap(str));
            str[sl] = '\0';
            strcat(str, ".MAT");
            gMaterial[mat] = LoadMaterial(str);
            if (gMaterial[mat] == NULL) {
                BrFatal("C:\\Msdev\\Projects\\DethRace\\SKIDMARK.C", 207, "Couldn't find %s", gMaterial_names[mat]);
            } else {
#ifdef DETHRACE_3DFX_PATCH
                GlorifyMaterial(&gMaterial[mat], 1);
#endif
                BrMaterialAdd(gMaterial[mat]);
            }
        }
#ifdef DETHRACE_3DFX_PATCH
        else {

            BrMapRemove(gMaterial[mat]->colour_map);
            gMaterial[mat]->colour_map = PurifiedPixelmap(gMaterial[mat]->colour_map);
            BrMapAdd(gMaterial[mat]->colour_map);
            GlorifyMaterial(&gMaterial[mat], 1);
            BrMaterialUpdate(gMaterial[mat], BR_MATU_ALL);
        }
#endif
    }

    // Added by dethrace
    // Baking a mark's world position once is only valid while
    // EnsureGroundDetailVisible leaves marks where they were laid, which is a
    // DETHRACE_FIX_BUGS behaviour; the original slides them toward the camera
    // every frame. Refuse to bake there rather than render them wrongly.
#if defined(DETHRACE_FIX_BUGS)
    gSkid_batching = harness_game_config.persistent_skids;
#else
    gSkid_batching = 0;
#endif
    // Added by dethrace
    // The ring size follows from that one decision and is not separately
    // tunable: the original's fixed 100 without batching, and as many as the
    // index type allows with it. Giving marks their own actor is what makes a
    // big ring unaffordable, so the two were never independent.
    gSkid_count = gSkid_batching ? SKID_RING_PERSISTENT : SKID_RING_CLASSIC;

    gSkids = BrMemAllocate(gSkid_count * sizeof(tSkid), kMem_misc);
    gSkid_in_use = BrMemAllocate(gSkid_count, kMem_misc);
    memset(gSkid_in_use, 0, gSkid_count);
    if (gSkid_batching) {
        gSkid_u0 = BrMemAllocate(gSkid_count * sizeof(br_scalar), kMem_misc);
        gSkid_u1 = BrMemAllocate(gSkid_count * sizeof(br_scalar), kMem_misc);
        for (skid = 0; skid < gSkid_count; skid++) {
            gSkid_u0[skid] = 0.0f;
            gSkid_u1[skid] = 1.0f;
        }
    }
    for (skid = 0; skid < gSkid_count; skid++) {
        gSkids[skid].actor = BrActorAllocate(BR_ACTOR_MODEL, NULL);
        // Added by dethrace
        // When batching, these actors are state carriers only - matrix,
        // material and texture coordinates - so they stay out of the tree and
        // the renderer never sees them.
        if (!gSkid_batching) {
            BrActorAdd(gNon_track_actor, gSkids[skid].actor);
        }
        gSkids[skid].actor->t.t.mat.m[1][1] = 0.01f;
        gSkids[skid].actor->render_style = BR_RSTYLE_NONE;
        // Added by dethrace
        // When batching, every mark's model holds the identical unit square and
        // is never rendered - it exists only to carry the two texture
        // coordinates StretchMark writes, which BakeSkidIntoBatch reads back
        // immediately afterwards. One shared scratch model does that job, and
        // saves gSkid_count model structs, their kept originals, their prepared
        // copies and a GL buffer object each.
        if (gSkid_batching && skid > 0) {
            gSkids[skid].actor->model = gSkids[0].actor->model;
            continue;
        }
        square = BrModelAllocate(NULL, 4, 2);
        BrVector3Set(&square->vertices[0].p, -0.5f, 1.0f, -0.5f);
        BrVector3Set(&square->vertices[1].p, -0.5f, 1.0f, 0.5f);
        BrVector3Set(&square->vertices[2].p, 0.5f, 1.0f, 0.5f);
        BrVector3Set(&square->vertices[3].p, 0.5f, 1.0f, -0.5f);
        BrVector2Set(&square->vertices[0].map, 0.0f, 0.0f);
        BrVector2Set(&square->vertices[1].map, 0.0f, 1.0f);
        BrVector2Set(&square->vertices[2].map, 1.0f, 1.0f);
        BrVector2Set(&square->vertices[3].map, 1.0f, 0.0f);
        square->faces[0].vertices[0] = 0;
        square->faces[0].vertices[1] = 1;
        square->faces[0].vertices[2] = 2;
        square->faces[0].smoothing = 1;
        square->faces[1].vertices[0] = 0;
        square->faces[1].vertices[1] = 2;
        square->faces[1].vertices[2] = 3;
        square->faces[1].smoothing = 1;
        square->flags |= BR_MODF_KEEP_ORIGINAL;
        BrModelAdd(square);
        gSkids[skid].actor->model = square;
    }

    // Added by dethrace
    // One batch per SKID_BATCH_QUADS marks, allocated up front so the count is
    // fixed at gSkid_count / SKID_BATCH_QUADS and cannot grow during a race.
    if (gSkid_batching) {
        int batch;
        int quad;
        br_model* model;

        gSkid_batch_count = (gSkid_count + SKID_BATCH_QUADS - 1) / SKID_BATCH_QUADS;
        gSkid_batches = BrMemAllocate(gSkid_batch_count * sizeof(tSkid_batch), kMem_misc);
        memset(gSkid_batches, 0, gSkid_batch_count * sizeof(tSkid_batch));

        for (batch = 0; batch < gSkid_batch_count; batch++) {
            model = BrModelAllocate(NULL, SKID_BATCH_QUADS * 4, SKID_BATCH_QUADS * 2);
            for (quad = 0; quad < SKID_BATCH_QUADS; quad++) {
                int v0 = quad * 4;
                int f0 = quad * 2;

                // Corners match the per-mark square, so its texture coordinates
                // carry over once the vertices are baked.
                BrVector2Set(&model->vertices[v0 + 0].map, 0.0f, 0.0f);
                BrVector2Set(&model->vertices[v0 + 1].map, 0.0f, 1.0f);
                BrVector2Set(&model->vertices[v0 + 2].map, 1.0f, 1.0f);
                BrVector2Set(&model->vertices[v0 + 3].map, 1.0f, 0.0f);

                model->faces[f0 + 0].vertices[0] = v0 + 0;
                model->faces[f0 + 0].vertices[1] = v0 + 1;
                model->faces[f0 + 0].vertices[2] = v0 + 2;
                model->faces[f0 + 0].smoothing = 1;
                model->faces[f0 + 1].vertices[0] = v0 + 0;
                model->faces[f0 + 1].vertices[1] = v0 + 2;
                model->faces[f0 + 1].vertices[2] = v0 + 3;
                model->faces[f0 + 1].smoothing = 1;
            }
            model->flags |= BR_MODF_KEEP_ORIGINAL;
            BrModelAdd(model);

            gSkid_batches[batch].model = model;
            gSkid_batches[batch].actor = BrActorAllocate(BR_ACTOR_MODEL, NULL);
            gSkid_batches[batch].actor->model = model;
            // Vertices are already in world space, so the actor adds no
            // transform of its own.
            BrMatrix34Identity(&gSkid_batches[batch].actor->t.t.mat);
            gSkid_batches[batch].actor->render_style = BR_RSTYLE_NONE;
            BrActorAdd(gNon_track_actor, gSkid_batches[batch].actor);
        }
    }
}


// IDA: void __usercall HideSkid(int pSkid_num@<EAX>)
// FUNCTION: CARM95 0x0040148d
void HideSkid(int pSkid_num) {

    gSkids[pSkid_num].actor->render_style = BR_RSTYLE_NONE;
    // Added by dethrace
    ClearSkidFromBatch(pSkid_num);
}

// IDA: void __cdecl HideSkids()
// FUNCTION: CARM95 0x004014ad
void HideSkids(void) {
    int skid;

    for (skid = 0; skid < gSkid_count; skid++) {
        HideSkid(skid);
    }
}

// IDA: void __usercall SkidMark(tCar_spec *pCar@<EAX>, int pWheel_num@<EDX>)
// FUNCTION: CARM95 0x004014e5
void SkidMark(tCar_spec* pCar, int pWheel_num) {
    br_vector3 pos;
    br_vector3 world_pos;
    br_vector3 disp;
    br_vector3 spesh_to_wheel;
    int material_index;
    br_scalar dist;
    br_scalar dist2;
    int on_ground;
    br_material* material;

    on_ground = pCar->susp_height[pWheel_num >> 1] > pCar->oldd[pWheel_num];
    if (!on_ground) {
        pCar->special_start[pWheel_num].v[0] = FLT_MAX;
    }
    if (pCar->blood_remaining[pWheel_num] != 0 && on_ground) {
        pCar->new_skidding |= 1 << pWheel_num;
        material_index = -3;
    } else if (pCar->oil_remaining[pWheel_num] != 0 && on_ground) {
        pCar->new_skidding |= 1 << pWheel_num;
        material_index = -2;
    } else {
        material_index = pCar->material_index[pWheel_num];
        material = gCurrent_race.material_modifiers[material_index].skid_mark_material;
        if (material == NULL) {
            pCar->old_skidding &= ~(1 << pWheel_num);
            return;
        }
    }

    if (((1 << pWheel_num) & pCar->new_skidding) != 0 || ((1 << pWheel_num) & pCar->old_skidding) != 0) {
        if ((pWheel_num & 1) != 0) {
            pos.v[0] = pCar->bounds[1].max.v[0] - 0.1725f;
        } else {
            pos.v[0] = pCar->bounds[1].min.v[0] + 0.1725f;
        }
        pos.v[1] = pCar->wpos[pWheel_num].v[1] - pCar->oldd[pWheel_num];
        pos.v[2] = pCar->wpos[pWheel_num].v[2];
        BrMatrix34ApplyP(&world_pos, &pos, &pCar->car_master_actor->t.t.mat);
        BrVector3InvScale(&world_pos, &world_pos, WORLD_SCALE);
        if (pCar->special_start[pWheel_num].v[0] != FLT_MAX) {

            BrVector3Sub(&spesh_to_wheel, &world_pos, &pCar->special_start[pWheel_num]);
            dist2 = BrVector3Length(&spesh_to_wheel);
            if (dist2 <= BR_SCALAR_EPSILON || (BrVector3Dot(&pCar->direction, &spesh_to_wheel) / dist2 < 0.70700002f)) {
                return;
            }
            world_pos = pCar->special_start[pWheel_num];
            pCar->special_start[pWheel_num].v[0] = FLT_MAX;
        }
        if (((1 << pWheel_num) & pCar->new_skidding) != 0) {
            if (((1 << pWheel_num) & pCar->old_skidding) != 0) {
                BrVector3Sub(&disp, &world_pos, &pCar->prev_skid_pos[pWheel_num]);
                dist = BrVector3Length(&disp);
                if (dist < 0.05f) {
                    return;
                }
                SkidSection(pCar, pWheel_num, &world_pos, material_index);
                pCar->total_length[pWheel_num] = pCar->total_length[pWheel_num] + dist;
                pCar->oil_remaining[pWheel_num] = pCar->oil_remaining[pWheel_num] - dist;
                if (pCar->oil_remaining[pWheel_num] < 0.0f) {
                    pCar->oil_remaining[pWheel_num] = 0.0f;
                }
                pCar->blood_remaining[pWheel_num] = pCar->blood_remaining[pWheel_num] - dist;
                if (pCar->blood_remaining[pWheel_num] < 0.0f) {
                    pCar->blood_remaining[pWheel_num] = 0.0f;
                }
            } else {
                pCar->old_skidding |= 1 << pWheel_num;
                pCar->total_length[pWheel_num] = 0.0f;
                pCar->old_skid[pWheel_num] = -1;
            }
        } else {
            pCar->old_skidding &= ~(1 << pWheel_num);
        }
        pCar->prev_skid_pos[pWheel_num] = world_pos;
        pCar->prev_nor[pWheel_num] = pCar->nor[pWheel_num];
    }
}

// IDA: void __usercall SkidSection(tCar_spec *pCar@<EAX>, int pWheel_num@<EDX>, br_vector3 *pPos@<EBX>, int pMaterial_index@<ECX>)
// FUNCTION: CARM95 0x00401a22
void SkidSection(tCar_spec* pCar, int pWheel_num, br_vector3* pPos, int pMaterial_index) {
    // GLOBAL: CARM95 0x530c88
    static tU16 skid;
    br_material* material;

    if (BrVector3Dot(&pCar->prev_nor[pWheel_num], &pCar->nor[pWheel_num]) < 0.99699998f
        || BR_ABS(BrVector3Dot(&pCar->nor[pWheel_num], pPos) - BrVector3Dot(&pCar->prev_skid_pos[pWheel_num], &pCar->nor[pWheel_num])) > 0.01f) {
        pCar->old_skidding &= ~(1 << pWheel_num);
        pCar->old_skid[pWheel_num] = -1;
        return;
    }

    material = MaterialFromIndex(pMaterial_index);
    if (pCar->old_skid[pWheel_num] >= gSkid_count
        || gSkids[pCar->old_skid[pWheel_num]].actor->material != material
        || SkidLen(pCar->old_skid[pWheel_num]) > 0.5f
        || FarFromLine2D(pPos, &pCar->skid_line_start[pWheel_num], &pCar->skid_line_end[pWheel_num])
        || Reflex2D(pPos, &pCar->skid_line_start[pWheel_num], &pCar->prev_skid_pos[pWheel_num])) {

        pCar->skid_line_start[pWheel_num] = pCar->prev_skid_pos[pWheel_num];
        pCar->skid_line_end[pWheel_num] = *pPos;
        gSkids[skid].actor->render_style = BR_RSTYLE_DEFAULT;
        gSkids[skid].actor->material = material;
        gSkids[skid].normal = pCar->nor[pWheel_num];
        StretchMark(&gSkids[skid], &pCar->prev_skid_pos[pWheel_num], pPos, pCar->total_length[pWheel_num]);
        PipeSingleSkidAdjustment(skid, &gSkids[skid].actor->t.t.mat, pMaterial_index);
        pCar->old_skid[pWheel_num] = skid;
        skid = (skid + 1) % gSkid_count;
    } else {
        StretchMark(&gSkids[pCar->old_skid[pWheel_num]], &pCar->skid_line_start[pWheel_num], pPos, pCar->total_length[pWheel_num]);
        PipeSingleSkidAdjustment(pCar->old_skid[pWheel_num], &gSkids[pCar->old_skid[pWheel_num]].actor->t.t.mat, pMaterial_index);
    }
}

// IDA: void __usercall StretchMark(tSkid *pMark@<EAX>, br_vector3 *pFrom@<EDX>, br_vector3 *pTo@<EBX>, br_scalar pTexture_start)
// FUNCTION: CARM95 0x00401e7c
void StretchMark(tSkid* pMark, br_vector3* pFrom, br_vector3* pTo, br_scalar pTexture_start) {
    br_vector3 temp;
    br_vector3* rows;
    br_scalar len;
    br_model* model;

    rows = (br_vector3*)&pMark->actor->t.t.mat;
    BrVector3Sub(&temp, pTo, pFrom);
    len = BrVector3Length(&temp);

    if (len < BR_SCALAR_EPSILON) {
        rows[2].v[0] = pMark->normal.v[2] * temp.v[1] - pMark->normal.v[1] * temp.v[2];
        rows[2].v[1] = pMark->normal.v[0] * temp.v[2] - pMark->normal.v[2] * temp.v[0];
        rows[2].v[2] = pMark->normal.v[1] * temp.v[0] - pMark->normal.v[0] * temp.v[1];
    } else {
        rows[2].v[0] = pMark->normal.v[2] * temp.v[1] - pMark->normal.v[1] * temp.v[2];
        rows[2].v[1] = pMark->normal.v[0] * temp.v[2] - pMark->normal.v[2] * temp.v[0];
        rows[2].v[2] = pMark->normal.v[1] * temp.v[0] - pMark->normal.v[0] * temp.v[1];
        rows[2].v[0] = 0.05f / len * rows[2].v[0];
        rows[2].v[1] = 0.05f / len * rows[2].v[1];
        rows[2].v[2] = 0.05f / len * rows[2].v[2];
        rows->v[0] = len / len * temp.v[0];
        rows->v[1] = len / len * temp.v[1];
        rows->v[2] = len / len * temp.v[2];
        BrVector3Add(&temp, pTo, pFrom);
        BrVector3Scale(&pMark->pos, &temp, 0.5f);
        rows[3] = pMark->pos;
#if defined(DETHRACE_FIX_BUGS)
        // Lift model vertices (at y=1.0) 0.005 BU along the surface normal so
        // skid marks render on top of car shadows. The shadow is rendered in a
        // separate BrZbScene pass before the main scene and leaves exact ground
        // depth values in the depth buffer. At typical follow-cam distances
        // (15-30 BU) a 0.001 BU offset produces only 1-3 depth-buffer units of
        // separation, which collapses to zero under GPU rounding; 0.005 gives
        // ~6-15 units, reliably beating the shadow. The offset is along the
        // surface normal (not world Y) so it stays flush on sloped surfaces.
        BrVector3Scale(&rows[1], &pMark->normal, 0.005f);
#endif
        model = pMark->actor->model;
        model->vertices[1].map.v[0] = pTexture_start / 0.05f;
        model->vertices[0].map.v[0] = model->vertices[1].map.v[0];
        model->vertices[3].map.v[0] = (pTexture_start + len) / 0.05f;
        model->vertices[2].map.v[0] = model->vertices[3].map.v[0];
        // Added by dethrace
        // When batching, the per-mark model is state only - never rendered - so
        // updating it would upload geometry nothing draws. The batch it feeds
        // is updated once a frame instead.
        if (gSkid_batching) {
            int mark = (int)(pMark - gSkids);

            gSkid_u0[mark] = model->vertices[0].map.v[0];
            gSkid_u1[mark] = model->vertices[3].map.v[0];
            BakeSkidIntoBatch(mark);
        } else {
            BrModelUpdate(model, BR_MODU_ALL);
        }
    }
}

// IDA: int __usercall FarFromLine2D@<EAX>(br_vector3 *pPt@<EAX>, br_vector3 *pL1@<EDX>, br_vector3 *pL2@<EBX>)
// FUNCTION: CARM95 0x004020dc
int FarFromLine2D(br_vector3* pPt, br_vector3* pL1, br_vector3* pL2) {
    br_vector2 line;
    br_vector2 to_pt;
    br_scalar line_len;
    br_scalar cross;

    line.v[0] = BR_SUB(pL2->v[0], pL1->v[0]);
    line.v[1] = BR_SUB(pL2->v[2], pL1->v[2]);
    to_pt.v[0] = BR_SUB(pPt->v[0], pL2->v[0]);
    to_pt.v[1] = BR_SUB(pPt->v[2], pL2->v[2]);

    cross = (-line.v[0]) * to_pt.v[1] + to_pt.v[0] * line.v[1];
    line_len = BrVector2Length(&line);
    if (fabs(cross) > line_len * 0.05f) {
        return 1;
    } else {
        return 0;
    }
}

// IDA: int __usercall Reflex2D@<EAX>(br_vector3 *pPt@<EAX>, br_vector3 *pL1@<EDX>, br_vector3 *pL2@<EBX>)
// FUNCTION: CARM95 0x00402179
int Reflex2D(br_vector3* pPt, br_vector3* pL1, br_vector3* pL2) {
    br_vector2 line;
    br_vector2 to_pt;

    line.v[0] = pL2->v[0] - pL1->v[0];
    line.v[1] = pL2->v[2] - pL1->v[2];
    to_pt.v[0] = pPt->v[0] - pL2->v[0];
    to_pt.v[1] = pPt->v[2] - pL2->v[2];
    if (BrVector2Dot(&to_pt, &line) < 0.0f) {
        return 1;
    } else {
        return 0;
    }
}

// IDA: br_scalar __usercall SkidLen@<ST0>(int pSkid@<EAX>)
// FUNCTION: CARM95 0x004021f1
br_scalar SkidLen(int pSkid) {
    return sqrt(
        gSkids[pSkid].actor->t.t.mat.m[0][2] * gSkids[pSkid].actor->t.t.mat.m[0][2]
        + gSkids[pSkid].actor->t.t.mat.m[0][1] * gSkids[pSkid].actor->t.t.mat.m[0][1]
        + gSkids[pSkid].actor->t.t.mat.m[0][0] * gSkids[pSkid].actor->t.t.mat.m[0][0]);
}

// IDA: void __usercall InitCarSkidStuff(tCar_spec *pCar@<EAX>)
// FUNCTION: CARM95 0x00402282
void InitCarSkidStuff(tCar_spec* pCar) {
    int wheel;

    pCar->old_skidding = 0;
    for (wheel = 0; wheel < 4; wheel++) {
        pCar->special_start[wheel].v[0] = FLT_MAX;
        pCar->blood_remaining[wheel] = 0.0f;
        pCar->oil_remaining[wheel] = 0.0f;
    }
}

// IDA: void __cdecl SkidsPerFrame()
// FUNCTION: CARM95 0x004022f1
void SkidsPerFrame(void) {
    int skid;

    // Added by dethrace
    // Batched path: push whatever geometry was laid down since the last frame.
    // Marks go into consecutive quads, so normally only the batch currently
    // being filled is dirty. Visibility is left to BRender's frustum test -
    // with gSkid_count / SKID_BATCH_QUADS actors instead of one per mark, its
    // per-actor cost no longer matters.
    if (gSkid_batching) {
        gSkid_batches_live = 0;
        for (skid = 0; skid < gSkid_batch_count; skid++) {
            tSkid_batch* batch = &gSkid_batches[skid];

            if (!batch->occupied) {
                continue;
            }
            gSkid_batches_live++;
            if (batch->dirty) {
                BrModelUpdate(batch->model, BR_MODU_ALL);
                batch->dirty = 0;
            }
            batch->actor->render_style = BR_RSTYLE_DEFAULT;
        }
        return;
    }

    for (skid = 0; skid < gSkid_count; skid++) {
        if (gSkids[skid].actor->render_style != BR_RSTYLE_NONE) {
            EnsureGroundDetailVisible(&gSkids[skid].actor->t.t.translate.t, &gSkids[skid].normal, &gSkids[skid].pos);
        }
    }
}

// IDA: void __cdecl RemoveMaterialsFromSkidmarks()
void RemoveMaterialsFromSkidmarks(void) {
    int skid;
#if defined(DETHRACE_FIX_BUGS)
    // Added by dethrace
    int batch;
    int f;
    br_model* model;
#endif

    for (skid = 0; skid < gSkid_count; skid++) {
        gSkids[skid].actor->material = NULL;
    }

#if defined(DETHRACE_FIX_BUGS)
    // Added by dethrace
    // Batching holds each material a second time, on the shared models' faces.
    // This function exists because the race's materials are about to be freed,
    // and the per-mark actors above are no longer the only thing referencing
    // them -- so the batches have to be released here too. The batch models
    // outlive the race (InitSkids runs once, from InitialiseApplication) and
    // stay parented to gNon_track_actor, which FrobFog walks at the start of
    // every race, calling BrMaterialUpdate on every face material it finds.
    // Leave these set and the second race of a session dereferences a freed
    // material.
    if (gSkid_batching && gSkid_batches != NULL) {
        for (batch = 0; batch < gSkid_batch_count; batch++) {
            model = gSkid_batches[batch].model;
            if (model == NULL) {
                continue;
            }
            for (f = 0; f < model->nfaces; f++) {
                model->faces[f].material = NULL;
            }
        }
    }
#endif
}
