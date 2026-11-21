#ifndef MH_SCENE_MATRICES_H
#define MH_SCENE_MATRICES_H

#include "engine/simulation/field.h"

#define MH_CAR_MESHES 5
#define MH_DRIVE_MESHES (1 + MH_CAR_MESHES)
#define MH_FIELD_MESHES (1 + MH_CAR_MESHES * MH_RACE_MAX_CARS)

/*
 * Six column-major transforms: world, car body, FL FR BR BL. Camera uses focal
 * 1, near .5 and TRK far distance. Viewport correction retains the 4:3
 * composition at other aspects. Wheel suspension/steering offsets retain
 * front/rear rates and angle integration. Uses wheel angles already advanced at
 * the presentation boundary. Requires an initialized session, aspect in
 * [.01,100] and non-null output arrays.
 */
void	mh_drive_matrices(const struct mh_drive *drive, double aspect,
	float models[MH_DRIVE_MESHES * 16], float matrix[16], float view[16]);

/* Shared world transform, five models per car and the selected camera view. */
void	mh_field_matrices(const struct mh_field *field, double aspect,
	float models[MH_FIELD_MESHES * 16], float matrix[16], float view[16]);

#endif
