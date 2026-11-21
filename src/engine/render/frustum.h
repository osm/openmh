#ifndef MH_FRUSTUM_H
#define MH_FRUSTUM_H

#include "engine/core/error.h"
#include "engine/render/profile.h"
#define MH_FRUSTUM_PLANES 6

/*
 * Original object-culling planes, outward normals in world space.
 * Their vertical bounds differ from the point-projection viewport.
 */
struct mh_frustum {
	float	planes[MH_FRUSTUM_PLANES][4];
};

/*
 * Prepare the aspect ratio, local frustum and world planes. Lens inputs have
 * already passed camera clamps. pose is the camera's positive-Z, column-major
 * 12-float world transform. Invalid input leaves output unchanged.
 */
enum mh_error_code	mh_frustum_setup(unsigned int width,
	unsigned int height, float focal, float near_distance,
	float far_distance, const float pose[12], struct mh_frustum *output,
	struct mh_error *error_out);

/*
 * Touching a plane remains visible. Invalid inputs return zero. The sphere uses
 * the root MYO radius, without pose scaling.
 */
int	mh_frustum_sphere(const struct mh_frustum *frustum,
	const float position[3], float radius);

/*
 * Software rounds world-plane transforms and sphere tests to scene binary32.
 * Cached local lens planes are constructed before the scene precision switch.
 * Hardware profiles accumulate dot products in long double.
 */
enum mh_error_code	mh_frustum_setup_profile(enum mh_render_profile profile,
	unsigned int width, unsigned int height, float focal,
	float near_distance, float far_distance, const float pose[12],
	struct mh_frustum *output, struct mh_error *error_out);
int			mh_frustum_sphere_profile(
	enum mh_render_profile profile, const struct mh_frustum *frustum,
	const float position[3], float radius);

#endif
