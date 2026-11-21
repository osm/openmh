#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_tracks.h"
#include "engine/core/error.h"
#include "engine/render/scene_internal.h"
#include "engine/render/texture_policy.h"
#include "interfaces/image/atlas_source.h"

/* Software demos have one palette collection, shared by all materials. */
enum mh_error_code
mh_race_scene_open_opaque_textures(const struct mh_resource_source *resources,
	const char *root, const struct mh_race_scene *scene,
	struct mh_image_collection **output, struct mh_error *error_out)
{
	char relative[MH_TRACK_PATH_SIZE + 32];
	enum mh_error_code status;

	sprintf(relative, MH_ASSET_TRACK_TEXTURES_SECONDARY_FORMAT,
		scene->assets.track.base_path);
	status = mh_image_collection_open(scene->image_source, resources, root,
		relative, output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	sprintf(relative, MH_ASSET_TRACK_TEXTURES_PRIMARY_FORMAT,
		scene->assets.track.base_path);

	return mh_image_collection_open(scene->image_source, resources, root,
		relative, output, error_out);
}

enum mh_error_code
mh_race_scene_register_textures(const struct mh_resource_source *resources,
	const char *root, const struct mh_image_collection *cutout,
	const struct mh_image_collection *opaque, struct mh_race_scene *scene,
	unsigned int mesh_count, struct mh_error *error_out)
{
	const struct mh_texture_policy *policy;
	struct mh_atlas_map remaps[3];
	const struct mh_atlas_map *remap;
	struct mh_texture_groups groups;
	struct mh_atlas_options options;
	enum mh_error_code status;
	char relative[MH_TRACK_PATH_SIZE + 32];
	unsigned int i, first, last;

	/*
	 * Initial loading reads all three archives as the original did. Later
	 * opponent loads reuse the selected page owner and its budget.
	 */
	if (scene->assets.texture_quality > 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid retained atlas quality");

	first = scene->assets.texture_atlas.pages == NULL
		? 0
		: scene->assets.texture_quality;
	last = scene->assets.texture_atlas.pages == NULL ? 2 : first;
	memset(remaps, 0, sizeof(remaps));
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	options.opaque_format = scene->assets.opaque_format;
	options.alpha_format = scene->assets.alpha_format;
	policy = mh_scene_texture_policy(scene->assets.profile);
	options.shared_palette = policy->shared_palette;
	options.border = scene->assets.texture_atlas.border;

	for (i = first; status == MH_ERROR_NONE && i <= last; i++) {
		sprintf(relative, MH_ASSET_TRACK_REMAP_FORMAT,
			scene->assets.track.base_path, i,
			options.border != 0 ? "m" : "");
		status = mh_atlas_map_load_from(scene->atlas_source, resources,
			root, relative, &remaps[i], error_out);

		/*
		 * No authored atlas: retain the decoded, individual textures.
		 */
		if (status == MH_ERROR_NOT_FOUND && i == 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	memset(&groups, 0, sizeof(groups));

	if (status == MH_ERROR_NONE &&
		scene->assets.texture_atlas.pages == NULL) {
		status = policy->choose_atlas(&remaps[0], &remaps[1],
			scene->assets.texture_budget, options.opaque_format,
			options.border ? 3 : 1, &scene->assets.texture_quality,
			error_out);
	}

	remap = &remaps[scene->assets.texture_quality];

	if (status == MH_ERROR_NONE &&
		scene->assets.texture_atlas.pages == NULL)
		status = mh_atlas_build(scene->image_decoder, remap, cutout,
			options.shared_palette ? opaque : cutout,
			&scene->assets.track, &options,
			&scene->assets.texture_atlas, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < mesh_count; i++)
		status = mh_mesh_register_textures(
			&scene->assets.meshes[i], remap, &groups, error_out);

	if (status == MH_ERROR_NONE)
		scene->assets.texture_groups = groups;

	for (i = 0; status == MH_ERROR_NONE && i < mesh_count; i++)
		status = mh_mesh_bind_atlas(&scene->assets.meshes[i], remap,
			&scene->assets.texture_atlas, error_out);

	for (i = 0; i < 3; i++)
		mh_atlas_map_free(&remaps[i]);

	return status;
}

enum mh_error_code
mh_race_scene_texture_budget(enum mh_render_profile profile,
	const unsigned long *override, unsigned long *budget,
	struct mh_error *error_out)
{
	unsigned long maximum;

	maximum = mh_scene_texture_policy(profile)->capacity;

	if (override != NULL && *override > maximum)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"texture budget exceeds rendering profile capacity");

	*budget = override == NULL ? maximum : *override;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
