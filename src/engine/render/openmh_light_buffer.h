#ifndef MH_OPENMH_LIGHT_BUFFER_H
#define MH_OPENMH_LIGHT_BUFFER_H

#include "engine/render/mesh.h"
#include "interfaces/render/lighting.h"

/*
 * std140 Lighting block: three vec4s followed by direction/color/view triples.
 * OpenMH retains the strongest 64 contributing lights per object. Selection
 * is stable on ties and independent of camera/frame timing.
 */
#define MH_OPENMH_LIGHTS 64
#define MH_OPENMH_LIGHT_FLOATS (12 + MH_OPENMH_LIGHTS * 12)

struct mh_openmh_object {
	struct mh_mesh_object	geometry;
	float			lighting[MH_OPENMH_LIGHT_FLOATS];
};

/* Owned GPU input preparation; no simulation state or GPU API objects. */
struct mh_openmh_light_buffer {
	struct mh_world			world;
	struct mh_model_lighting	settings;
	struct mh_openmh_object		*objects;
	size_t				count, root;
	int				enabled;
};

enum mh_error_code	mh_openmh_light_buffer_set(
	struct mh_openmh_light_buffer *state, const struct mh_mesh *mesh,
	size_t root, const struct mh_world *world,
	const struct mh_model_lighting *settings, struct mh_error *error_out);
void			mh_openmh_light_buffer_free(
	struct mh_openmh_light_buffer *state);

/* Prepare once per frame, before submitting this mesh's material draws. */
enum mh_error_code	mh_openmh_light_buffer_prepare(
	struct mh_openmh_light_buffer *state, const struct mh_mesh *mesh,
	const float *model, const float *root, struct mh_error *error_out);

/*
 * Surface is a vec4: base RGB, then 0=precolored, 1=lit, 2=emissive.
 * Returned lighting is borrowed until the next prepare/set/free.
 */
const float	*mh_openmh_light_buffer_surface(
	const struct mh_openmh_light_buffer *state, size_t first,
	float surface[4]);

#endif
