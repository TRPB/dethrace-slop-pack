// Added by dethrace
#ifndef ROUND_WHEELS_H
#define ROUND_WHEELS_H

#include "car.h"

// Regenerates a car's low-poly (octagonal) wheel models into rounder
// cylinders, and mirrors the left wheel of any pair whose shared model isn't
// symmetric about its axle. Gated by harness_game_config.round_wheels. Called
// once from LoadCar() right after pCar_spec->wheel_actors[] is resolved.
//
// pStorage_space takes ownership of any mirrored model created, so it is freed
// with the rest of the car; pass NULL to skip mirroring.
void RoundOffWheelActors(tCar_spec* pCar_spec, tBrender_storage* pStorage_space);

// Regenerates a single wheel model in place. Exposed (rather than kept
// internal to RoundOffWheelActors) so meld-scripts/wheeldump can run the real
// generator over every wheel model in the game data - see wheels.md.
// Silently leaves the model alone if its topology isn't one we recognize.
void RoundOffWheelModel(br_model* pModel);

// Builds a mirror image of a wheel model along the axle, as an unregistered
// model the caller owns. Exposed alongside RoundOffWheelModel so wheeldump can
// check it against the one mirrored pair the game itself ships (Splat Pack
// Vlad's Vdrwhl.dat / VDRRWHL.DAT) - see wheels.md.
br_model* BuildMirroredWheelModel(br_model* pSource, char* pName);

// True if mirroring along the axle would leave the model unchanged.
int WheelModelIsAxleSymmetric(br_model* pModel);

// Largest distance from any vertex to its nearest mirror-image partner across
// the axle plane. 0 means perfectly symmetric. Exposed so wheeldump can report
// the magnitude rather than just the yes/no answer.
double WheelModelAxleAsymmetry(br_model* pModel);

// Midpoint of the model's extent along the axle - the plane a wheel is
// mirrored about, which is not necessarily x=0.
br_scalar WheelModelAxleCentre(br_model* pModel);

// Which way along the axle the wheel's dished/detailed face points: +1 for the
// +x end, -1 for the -x end, 0 if symmetric.
int WheelModelDetailSide(br_model* pModel);

#endif
