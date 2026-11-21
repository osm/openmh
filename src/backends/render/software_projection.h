#ifndef MH_SOFTWARE_PROJECTION_H
#define MH_SOFTWARE_PROJECTION_H

#include "backends/render/software_near_clip.h"
#include "engine/render/projection.h"

struct mh_software_point {
	/* Screen XY, camera Z, 1/Z; camera XYZ for near points. */
	float		value[4];
	unsigned int	outcode;
};

/*
 * Native affine composition; a NULL model means world coordinates.
 * Camera is scratch output and may be partly written on a range failure.
 */
int	mh_software_camera_transform(const float view[16], const float *model,
	float camera[12]);

enum mh_error_code	mh_software_projection_check(
	const struct mh_projection *projection, struct mh_error *error_out);

/*
 * Project in scene binary32 precision. Transform is column-major, 12 floats,
 * positive-forward Z. Near equality is behind; far equality culls. Near points
 * retain value[3]; far points retain all values. Errors preserve output. The
 * supplied lens caches are consumed as stored, without rebuilding.
 */
enum mh_error_code	mh_software_project_point(
	const struct mh_projection *projection, const float transform[12],
	const float point[3], struct mh_software_point *output,
	struct mh_error *error_out);

/* Reconstruct projection; near points already contain camera XYZ. */
enum mh_error_code	mh_software_projection_restore(
	const struct mh_projection *projection,
	const struct mh_software_point *point, float camera[3],
	struct mh_error *error_out);

/*
 * Corners carry local XYZ in camera[], plus texel UV and byte-range RGB.
 * Any far corner or common viewport outcode culls the whole source face.
 * Near crossings reconstruct from the point cache before splitting; fully
 * visible quads retain their original four corners. Errors preserve output;
 * a successful cull leaves faces unchanged and writes count zero. Optional
 * points_out receives all four source cache entries on success (including
 * culls), before near splitting; the unused fourth triangle entry is zero.
 * It must not overlap faces/count. Errors preserve every output.
 */
enum mh_error_code	mh_software_project_face(
	const struct mh_projection *projection, const float transform[12],
	unsigned int flags, const struct mh_near_corner corners[4],
	struct mh_software_clipped_face output[2], unsigned int *count,
	struct mh_software_point *points_out, struct mh_error *error_out);

#endif
