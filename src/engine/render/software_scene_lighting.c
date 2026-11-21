#include <float.h>
#include <string.h>

#include "engine/render/scene.h"

static int
within_float_range(double value)
{
	return value >= -FLT_MAX && value <= FLT_MAX;
}

enum mh_error_code
mh_race_scene_software_lighting(const struct mh_race_scene *scene,
	const struct mh_field *field, unsigned int mesh,
	const struct mh_av_settings *settings,
	struct mh_model_lighting *lighting, struct mh_error *error_out)
{
	struct mh_directional_light directional;
	struct mh_model_lighting next;
	const struct mh_track *track;
	const struct mh_drive *viewed;
	const double *brightness;
	enum mh_error_code status;
	unsigned int shading, i;
	int coarse;

	if (scene == NULL || field == NULL || lighting == NULL ||
		field->count < 1 || field->count > MH_RACE_MAX_CARS ||
		mesh >= 1 + MH_CAR_MESHES * field->count ||
		(settings != NULL &&
			settings->car_shading > MH_AV_SHADING_REFLECTION))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software scene lighting");

	track = &scene->assets.track;
	viewed = &field->cars[mh_field_viewed(field)];
	brightness = scene->visuals.brightness_ready
		? scene->visuals.brightness.object
		: track->object_brightness;

	if (!within_float_range(track->light_intensity) ||
		track->light_intensity < 0 ||
		!within_float_range(track->specular_factor))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software scene lighting outside float range");

	if (!scene->assets.lighting_world.has_directional)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software world directional light");

	directional = scene->assets.lighting_world.directional;
	memset(&next, 0, sizeof(next));
	status = mh_light_directional_color(directional.color,
		(float)directional.intensity, next.directional_color,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 3; i++) {
		if (!within_float_range(viewed->eye[i]) ||
			!within_float_range(track->object_ambient[i]) ||
			!within_float_range(brightness[i]))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software scene light vector outside float range");

		next.eye[i] = (float)viewed->eye[i];
		next.ambient[i] = (float)track->object_ambient[i];
		next.brightness[i] = (float)brightness[i];
		next.direction[i] = (float)directional.direction[i];
	}

	shading = settings == NULL ? MH_AV_SHADING_REFLECTION
				: settings->car_shading;
	coarse = mesh != 0 && scene->visuals.coarse[(mesh - 1) / MH_CAR_MESHES];

	/* Wheel children inherit the body's car mode and flags. */
	next.mode = mesh == 0 ? MH_LIGHT_OBJECT : MH_LIGHT_CAR;

	if (mesh != 0 &&
		(field->unavailable_mask &
			(1U << ((mesh - 1) / MH_CAR_MESHES))))
		next.mode = MH_LIGHT_UNAVAILABLE;

	next.flat_detail = coarse || shading == MH_AV_SHADING_FLAT;
	next.flat_car = !coarse && shading == MH_AV_SHADING_REFLECTION;
	next.intensity = (float)track->light_intensity;

	/* Clamp negative specular factors. */
	next.specular_scale =
		track->specular_factor < 0 ? 0 : (float)track->specular_factor;
	*lighting = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
