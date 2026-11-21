#ifndef MH_A3D20_GEOMETRY_H
#define MH_A3D20_GEOMETRY_H

#include "engine/geometry/world.h"

/*
 * The original 200-entry scratch stops when count reaches capacity minus one.
 */
#define MH_A3D20_TRIANGLES 199

struct mh_a3d20_triangle {
	/*
	 * Stable within the borrowed world: disk primitive offset plus quad
	 * part.
	 */
	size_t	tag;
	float	vertices[3][3];
};

struct mh_a3d20_geometry {
	struct mh_a3d20_triangle	triangles[MH_A3D20_TRIANGLES];
	size_t				count;
};

struct mh_a3d20_material {
	/*
	 * Original parameter order: broadband gain, then high-frequency level.
	 * The SDK documents their meanings; native filtering is approximate.
	 */
	float	reflectance[2], transmittance[2];
	float	reflection_gain, reflection_delay;
};

/*
 * Synchronous required callbacks. begin pushes the geometry matrix, binds the
 * shared material and begins triangles; end ends triangles and pops the matrix.
 * triangle sets the stable tag then emits three vertices, already Z-negated.
 * Begin/end still run for a successful empty collection. Driver failures must
 * not cancel later requests. Scene Clear/identity/Flush belong to the queue.
 */
struct mh_a3d20_geometry_sink {
	void		*context;
	void		(*begin)(void *context);
	void (*triangle)(
		void	*context, const struct mh_a3d20_triangle *triangle);
	void		(*end)(void *context);
};

void	mh_a3d20_material_defaults(struct mh_a3d20_material *material);

/*
 * Decoded immutable world, finite game-coordinate listener position. Scenery is
 * separate and is not collected by the original. No allocation; output is
 * unchanged on failure. A missing world is an invalid input, not an empty
 * successful batch. The original aborts on unsupported face types too.
 */
enum mh_error_code	mh_a3d20_geometry_collect(const struct mh_world *world,
	const float position[3], struct mh_a3d20_geometry *output,
	struct mh_error *error_out);

void	mh_a3d20_geometry_submit(const struct mh_a3d20_geometry *geometry,
	const struct mh_a3d20_geometry_sink *sink);

#endif
