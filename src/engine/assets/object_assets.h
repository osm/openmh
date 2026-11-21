#ifndef MH_ASSETS_OBJECT_ASSETS_H
#define MH_ASSETS_OBJECT_ASSETS_H

#include "engine/animation/scene.h"
#include "engine/assets/object_body.h"
#include "engine/core/error.h"
#include "engine/core/file.h"
#include "interfaces/catalog/track.h"
#include "interfaces/collision/data.h"
#include "interfaces/collision/source.h"
#include "interfaces/geometry/decoder.h"

struct mh_animation_source;

struct mh_object_asset {
	struct mh_object_body	body;
	size_t			scene_index, model_index;
};

struct mh_object_model {
	char			name[MH_SCENE_MODEL_NAME_SIZE];
	double			minimum[3], maximum[3];
	struct mh_collision	hull;
};

struct mh_object_assets {
	struct mh_scene		scene;
	struct mh_object_asset	*objects;
	struct mh_object_model	*models;
	size_t			count, model_count;
};

struct mh_resource_source;

/*
 * Owns scene/keyframes and deduplicated decoded hulls; no textures or GPU
 * state. Decoded model bounds configure physics. Atomic output; 64 MiB encoded
 * hull bank limit. The source is borrowed for the load; returned assets own
 * their bytes.
 */
enum mh_error_code	mh_object_assets_load_from(
	const struct mh_animation_source *animation_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_resource_source *source, const char *root,
	const struct mh_track *track, struct mh_object_assets *output,
	struct mh_error *error_out);

void	mh_object_assets_free(struct mh_object_assets *assets);

#endif
