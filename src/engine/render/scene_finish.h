#ifndef MH_SCENE_FINISH_H
#define MH_SCENE_FINISH_H

#include "engine/animation/scene.h"
#include "engine/render/mesh.h"
#include "interfaces/geometry/decoder.h"

struct mh_animation_source;

#define MH_FINISH_SIGNS 3

struct mh_resource_source;

/* Owned models and one shared, seconds-based motion. */
struct mh_finish_assets {
	struct mh_scene_object	motion;
	struct mh_mesh		meshes[MH_FINISH_SIGNS];
};

struct mh_finish_frame {
	/* Zero hides all signs; otherwise selects meshes[rank - 1]. */
	unsigned int	rank;
	float		model[16];
	float		color[3];
};

/* Output unchanged on failure. Free successful output with mh_finish_free. */
enum mh_error_code	mh_finish_load(
	const struct mh_animation_source *animation_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_resource_source *resources, const char *root,
	enum mh_render_profile profile, struct mh_finish_assets *output,
	struct mh_error *error_out);
void			mh_finish_free(struct mh_finish_assets *assets);

/*
 * Caller starts seconds at zero for each loaded race. Hidden ranks do not
 * advance it. Sample first, then advance by the displayed frame interval.
 * Camera is a column-major local-to-world transform, in game coordinates.
 * Output and clock remain unchanged on failure.
 */
enum mh_error_code	mh_finish_frame(const struct mh_finish_assets *assets,
	unsigned int rank, float elapsed, const float camera[16],
	float *seconds, struct mh_finish_frame *output,
	struct mh_error *error_out);

#endif
