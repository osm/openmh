#include <stdlib.h>

#include "engine/assets/path_effects.h"
#include "engine/image/load.h"
#include "engine/render/scene_internal.h"

enum mh_error_code
mh_race_scene_load_lamps_from(const struct mh_resource_source *resources,
	const char *root, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	struct mh_file glow, flare;
	enum mh_error_code status;

	status = mh_image_read_from(scene->image_decoder, resources, root,
		MH_ASSET_LAMP_GLOW, &glow, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_image_read_from(scene->image_decoder, resources, root,
		MH_ASSET_LAMP_FLARE, &flare, error_out);

	if (status != MH_ERROR_NONE) {
		free(glow.data);
		return status;
	}

	status = mh_lamps_create(scene->image_decoder,
		&scene->assets.lighting_world, glow.data, glow.size, flare.data,
		flare.size, &scene->visuals.lamps, error_out);

	if (status == MH_ERROR_NONE && MH_RENDER_BUILD_POWERVR &&
		scene->assets.profile == MH_RENDER_POWERVR)
		status = mh_race_scene_load_lamp_atlas(
			scene, &glow, &flare, error_out);

	free(flare.data);
	free(glow.data);

	return status;
}
