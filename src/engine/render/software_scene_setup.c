#include <string.h>

#include "engine/core/limits.h"
#include "engine/render/software_scene.h"

static enum mh_error_code
load_panorama(const struct mh_race_scene_assets *assets,
	struct mh_renderer *renderer, const struct mh_av_settings *settings,
	struct mh_error *error_out)
{
	struct mh_panorama panorama;
	double value;
	unsigned int i;

	memset(&panorama, 0, sizeof(panorama));
	panorama.image = settings == NULL || settings->sky_image
		? &assets->software.sky
		: NULL;
	panorama.horizon = (float)assets->track.horizon;
	memcpy(panorama.above, assets->track.sky_top, sizeof(panorama.above));
	memcpy(panorama.below, assets->track.sky_bottom,
		sizeof(panorama.below));

	for (i = 0; i < 3; i++) {
		value = assets->track.cue_color[i];
		panorama.clear[i] = (unsigned char)(value < 0 ? 0
				: value > MH_U8_MAX	      ? MH_U8_MAX
							: value);
	}

	/* Disable height cue in normal original race setup. */

	return mh_renderer_set_panorama(renderer, &panorama, error_out);
}

enum mh_error_code
mh_race_scene_software_setup(const struct mh_race_scene *scene,
	struct mh_renderer *renderer, unsigned int cars,
	unsigned int finish_count, const struct mh_av_settings *settings,
	struct mh_error *error_out)
{
	const struct mh_indexed_image *maps[MH_TRACK_ENVIRONMENTS];
	enum mh_error_code status;
	unsigned int i;

	if (scene == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing software scene");

	status = mh_race_scene_software_order(renderer,
		&scene->assets.meshes[0], cars, finish_count, error_out);

	if (status == MH_ERROR_NONE &&
		!mh_renderer_mesh_shadows(renderer, 1 + cars * MH_CAR_MESHES))
		status = mh_renderer_error(renderer, error_out);

	for (i = 0; i < MH_TRACK_ENVIRONMENTS; i++)
		maps[i] = &scene->assets.software.environment[i];

	if (status == MH_ERROR_NONE)
		status = mh_renderer_set_environment(renderer, maps, error_out);

	if (status == MH_ERROR_NONE)
		status = load_panorama(
			&scene->assets, renderer, settings, error_out);

	return status;
}
