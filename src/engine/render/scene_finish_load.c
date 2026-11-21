#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_effects.h"
#include "engine/geometry/load.h"
#include "engine/geometry/model.h"
#include "engine/image/load.h"
#include "engine/render/scene_finish.h"
#include "engine/render/scene_internal.h"
#include "interfaces/animation/source.h"

/* Pass the binary32 value 0x3c888889. */
#define FINISH_FRAME_SECONDS (1.0f / 60.0f)

void
mh_finish_free(struct mh_finish_assets *assets)
{
	unsigned int i;

	free(assets->motion.keys);

	for (i = 0; i < MH_FINISH_SIGNS; i++)
		mh_mesh_free(&assets->meshes[i]);

	memset(assets, 0, sizeof(*assets));
}

static enum mh_error_code
load_motion(const struct mh_animation_source *animation_source,
	const struct mh_resource_source *resources, const char *root,
	struct mh_scene_object *motion, struct mh_error *error_out)
{
	struct mh_scene_key *key;
	size_t i;
	unsigned int channel;
	enum mh_error_code status;

	status = mh_motion_load_from(animation_source, resources, root,
		MH_ASSET_FINISH_MOTION, motion, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < motion->key_count; i++) {
		key = &motion->keys[i];
		key->time =
			(float)((long double)key->time * FINISH_FRAME_SECONDS);
		key->tension = (float)key->tension;
		key->continuity = (float)key->continuity;
		key->bias = (float)key->bias;

		for (channel = 0; channel < 9; channel++)
			key->value[channel] = (float)key->value[channel];
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_finish_load(const struct mh_animation_source *animation_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_resource_source *resources, const char *root,
	enum mh_render_profile profile, struct mh_finish_assets *output,
	struct mh_error *error_out)
{
	static const char *paths[MH_FINISH_SIGNS] = {MH_ASSET_FINISH_FIRST,
		MH_ASSET_FINISH_SECOND, MH_ASSET_FINISH_THIRD};

	struct mh_finish_assets assets;
	struct mh_model model;
	unsigned int i;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing finish sign output");

	memset(&assets, 0, sizeof(assets));
	status = load_motion(
		animation_source, resources, root, &assets.motion, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < MH_FINISH_SIGNS; i++) {
		memset(&model, 0, sizeof(model));
		status = mh_model_load_from(model_decoder, resources, root,
			paths[i], &model, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_mesh_build_native_profile(NULL, &model,
				NULL, profile, &assets.meshes[i], error_out);

		mh_model_free(&model);
	}

	if (status != MH_ERROR_NONE)
		mh_finish_free(&assets);
	else
		*output = assets;

	return status;
}

enum mh_error_code
mh_race_scene_load_finish(const struct mh_resource_source *resources,
	const char *root, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	struct mh_finish_assets assets;
	struct mh_file file;
	unsigned int i;
	enum mh_error_code status;
	struct mh_atlas_map remap;
	struct mh_texture_groups groups;

	status = mh_finish_load(scene->animation_source, scene->model_decoder,
		resources, root, scene->assets.profile, &assets, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * The finish models have no named textures; their faces use group zero.
	 */
	memset(&remap, 0, sizeof(remap));
	memset(&groups, 0, sizeof(groups));

	for (i = 0; i < MH_FINISH_SIGNS && status == MH_ERROR_NONE; i++)
		status = mh_mesh_register_textures(
			&assets.meshes[i], &remap, &groups, error_out);

	if (status == MH_ERROR_NONE &&
		scene->assets.profile != MH_RENDER_SOFTWARE) {
		status = mh_image_read_from(scene->image_decoder, resources,
			root, scene->assets.track.environment[0], &file,
			error_out);

		if (status == MH_ERROR_NONE) {
			for (i = 0;
				i < MH_FINISH_SIGNS && status == MH_ERROR_NONE;
				i++)
				status = mh_mesh_set_environment(
					scene->image_decoder, &assets.meshes[i],
					file.data, file.size,
					scene->assets.opaque_format, error_out);

			free(file.data);
		}
	}

	if (status != MH_ERROR_NONE)
		mh_finish_free(&assets);
	else
		scene->assets.finish = assets;

	return status;
}
