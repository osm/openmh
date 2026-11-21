#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_material.h"
#include "backends/render/software_model_light.h"

static const float identity[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};

static int
valid_settings(const struct mh_model_lighting *settings)
{
	unsigned int i;

	if ((settings->mode != MH_LIGHT_OBJECT &&
		settings->mode != MH_LIGHT_CAR &&
		settings->mode != MH_LIGHT_UNAVAILABLE) ||
		(settings->flat_car != 0 && settings->flat_car != 1) ||
		(settings->flat_detail != 0 && settings->flat_detail != 1) ||
		!(settings->intensity >= 0 && settings->intensity <= FLT_MAX) ||
		!(settings->specular_scale >= -FLT_MAX &&
			settings->specular_scale <= FLT_MAX) ||
		(settings->light_count != 0 && settings->lights == NULL) ||
		settings->light_count >
			(size_t)-1 / sizeof(struct mh_point_light) ||
		settings->light_count >
			(size_t)-1 / sizeof(struct mh_prepared_light))
		return 0;

	for (i = 0; i < 3; i++)
		if (!(settings->eye[i] >= -FLT_MAX &&
			settings->eye[i] <= FLT_MAX) ||
			!(settings->ambient[i] >= -FLT_MAX &&
				settings->ambient[i] <= FLT_MAX) ||
			!(settings->direction[i] >= -FLT_MAX &&
				settings->direction[i] <= FLT_MAX) ||
			!(settings->directional_color[i] >= -FLT_MAX &&
				settings->directional_color[i] <= FLT_MAX) ||
			!(settings->brightness[i] >= 0 &&
				settings->brightness[i] <=
					MH_SOFTWARE_BRIGHTNESS_MAX))
			return 0;

	return 1;
}

void
mh_software_model_light_free(struct mh_software_model_light *state)
{
	free(state->lights);
	free(state->prepared);
	memset(state, 0, sizeof(*state));
}

enum mh_error_code
mh_software_model_light_set(struct mh_software_model_light *state,
	const struct mh_model_lighting *settings, struct mh_error *error_out)
{
	struct mh_software_model_light next;
	struct mh_prepared_light checked_light;
	size_t index;
	enum mh_error_code status;

	if (settings == NULL) {
		mh_software_model_light_free(state);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (!valid_settings(settings))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software model lighting");

	for (index = 0; index < settings->light_count; index++) {
		status = mh_software_light_prepare(&settings->lights[index],
			identity, settings->eye, settings->intensity,
			&checked_light, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	memset(&next, 0, sizeof(next));

	if (settings->light_count != 0) {
		next.lights =
			malloc(settings->light_count * sizeof(*next.lights));
		next.prepared =
			malloc(settings->light_count * sizeof(*next.prepared));

		if (next.lights == NULL || next.prepared == NULL) {
			mh_software_model_light_free(&next);
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory copying model lights");
		}

		memcpy(next.lights, settings->lights,
			settings->light_count * sizeof(*next.lights));
	}

	next.settings = *settings;
	next.settings.lights = next.lights;
	next.enabled = 1;
	mh_software_model_light_free(state);
	*state = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Original single-precision vector rotation, with optional transpose. */
static int
rotate_vector(const float transform[12], const float vector[3], int transpose,
	float output[3])
{
	float product[3], sum;
	long double value;
	unsigned int i, k;

	for (i = 0; i < 3; i++) {
		for (k = 0; k < 3; k++) {
			value = (long double)vector[k] *
				transform[transpose ? i * 3 + k : k * 3 + i];

			if (!(value >= -FLT_MAX && value <= FLT_MAX))
				return 0;

			product[k] = (float)value;
		}

		value = (long double)product[1] + product[0];

		if (!(value >= -FLT_MAX && value <= FLT_MAX))
			return 0;

		sum = (float)value;
		value = (long double)sum + product[2];

		if (!(value >= -FLT_MAX && value <= FLT_MAX))
			return 0;

		output[i] = (float)value;
	}

	return 1;
}

int
mh_software_model_light_prepare(
	struct mh_software_model_light *state, const float *model)
{
	float transform[12];
	size_t index;
	unsigned int i, k;

	memcpy(transform, identity, sizeof(transform));

	if (model != NULL)
		for (i = 0; i < 4; i++)
			for (k = 0; k < 3; k++)
				transform[i * 3 + k] = model[i * 4 + k];

	for (index = 0; index < state->settings.light_count; index++)
		if (mh_software_light_prepare(&state->lights[index], transform,
			state->settings.eye, state->settings.intensity,
			&state->prepared[index], NULL) != MH_ERROR_NONE)
			return 0;

	state->frame.lights = state->prepared;
	state->frame.count = state->settings.light_count;
	state->frame.specular_scale = state->settings.specular_scale;
	memcpy(state->frame.ambient, state->settings.ambient,
		sizeof(state->frame.ambient));
	memcpy(state->frame.directional_color,
		state->settings.directional_color,
		sizeof(state->frame.directional_color));

	return rotate_vector(transform, state->settings.direction, 1,
		state->frame.direction);
}

int
mh_software_model_reflects(
	const struct mh_software_model_light *state, unsigned int flags)
{
	return state->settings.mode != MH_LIGHT_UNAVAILABLE &&
		(flags & MH_SOFTWARE_MATERIAL_ENVIRONMENT_MASK) != 0 &&
		(state->settings.mode != MH_LIGHT_CAR ||
			state->settings.flat_car);
}

int
mh_software_model_light_color(const struct mh_software_model_light *state,
	const struct mh_mesh_face *face, const struct mh_vertex *vertex,
	const float camera[12], int near_clipped, float color[3])
{
	float normal[3], shade[3];
	enum mh_software_shade mode;

	if (!state->enabled)
		return 0;

	/* Software unavailable outline RGB. */
	if (state->settings.mode == MH_LIGHT_UNAVAILABLE) {
		color[0] = color[1] = 0;
		color[2] = 255;
		return 1;
	}

	if (face->type == 8 || face->type == 9) {
		if (mh_software_model_reflects(state, face->flags)) {
			if (mh_software_light_shade(&state->frame, NULL,
				MH_SOFTWARE_SHADE_UNIFORM, shade,
				NULL) != MH_ERROR_NONE)
				return 0;

			return mh_software_light_color(0, 0, face->color,
				state->settings.brightness, shade, color,
				NULL) == MH_ERROR_NONE;
		}

		mode = state->settings.flat_detail ? MH_SOFTWARE_SHADE_UNIFORM
						: MH_SOFTWARE_SHADE_NORMAL;

		if (mh_software_light_shade(&state->frame, vertex->normal, mode,
			shade, NULL) != MH_ERROR_NONE)
			return 0;

		/* Store clipped triangle RGB before the 0.75 scale. */

		return mh_software_light_vertex_color(face->color,
			state->settings.brightness, shade,
			state->settings.flat_detail &&
				(face->type == 9 || !near_clipped),
			color, NULL) == MH_ERROR_NONE;
	}

	if (!(face->flags & MH_SOFTWARE_MATERIAL_EMISSIVE)) {
		mode = MH_SOFTWARE_SHADE_NORMAL;
		memcpy(normal, vertex->normal, sizeof(normal));

		if (state->settings.mode == MH_LIGHT_CAR &&
			(face->type < 2 || state->settings.flat_car)) {
			mode = MH_SOFTWARE_SHADE_CAR;

			if (!rotate_vector(camera, face->plane, 0, normal))
				return 0;
		}

		if (mh_software_light_shade(&state->frame, normal, mode, shade,
			NULL) != MH_ERROR_NONE)
			return 0;
	}

	return mh_software_light_color(face->type, face->flags, face->color,
		state->settings.brightness, shade, color,
		NULL) == MH_ERROR_NONE;
}
