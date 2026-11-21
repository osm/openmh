#ifndef MH_SOFTWARE_SKID_FACE_H
#define MH_SOFTWARE_SKID_FACE_H

#include "backends/render/software_face.h"
#include "backends/render/software_projection.h"

struct mh_software_skid_face {
	/*
	 * Visibility feedback is set before near splitting, even for empty
	 * output.
	 */
	int				visible;
	unsigned int			bucket, count;
	struct mh_software_triangle	triangles[4];
};

/*
 * One software ribbon quad, after segment culling and lighting. Positions are
 * four ordered world XYZ points; color is lit RGB before fog/saturation. camera
 * is the positive-Z affine view transform. NULL cue disables fog. Errors
 * preserve output. No allocation.
 */
enum mh_error_code	mh_software_skid_face_prepare(
	const struct mh_projection *, const float camera[12],
	const float positions[12], const float color[3],
	const struct mh_depth_cue *cue, struct mh_software_skid_face *output,
	struct mh_error *error_out);

#endif
