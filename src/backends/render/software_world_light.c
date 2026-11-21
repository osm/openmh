#include <stdlib.h>
#include <string.h>

#include "backends/render/software_light_select.h"
#include "backends/render/software_world_light.h"

void
mh_software_world_light_free(struct mh_software_world_light *state)
{
	mh_software_model_light_free(&state->lighting);
	memset(state, 0, sizeof(*state));
}

enum mh_error_code
mh_software_world_light_set(struct mh_software_world_light *state,
	const struct mh_world *world, const struct mh_model_lighting *lighting,
	struct mh_error *error_out)
{
	struct mh_software_world_light next;
	enum mh_error_code status;

	if (lighting == NULL && world == NULL) {
		mh_software_world_light_free(state);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (world == NULL || world->cells == NULL || lighting == NULL ||
		lighting->light_count != 0 || lighting->lights != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software world lighting");

	memset(&next, 0, sizeof(next));
	status = mh_software_model_light_set(
		&next.lighting, lighting, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next.world = *world;
	next.capacity = (size_t)world->link_count;

	if (next.capacity > (size_t)-1 / sizeof(*next.lighting.lights) ||
		next.capacity > (size_t)-1 / sizeof(*next.lighting.prepared)) {
		mh_software_world_light_free(&next);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software world light capacity overflow");
	}

	if (next.capacity == state->capacity) {
		next.lighting.lights = state->lighting.lights;
		next.lighting.prepared = state->lighting.prepared;
		state->lighting.lights = NULL;
		state->lighting.prepared = NULL;
	} else if (next.capacity != 0) {
		next.lighting.lights =
			malloc(next.capacity * sizeof(*next.lighting.lights));
		next.lighting.prepared =
			malloc(next.capacity * sizeof(*next.lighting.prepared));

		if (next.lighting.lights == NULL ||
			next.lighting.prepared == NULL) {
			mh_software_world_light_free(&next);
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory preparing world lights");
		}
	}

	next.lighting.settings.lights = next.lighting.lights;
	mh_software_world_light_free(state);
	*state = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_software_world_light_prepare(struct mh_software_world_light *state,
	const float *model, const float *root)
{
	static const float origin[3] = {0, 0, 0};

	if (!state->lighting.enabled ||
		mh_software_light_select(&state->world,
			root == NULL ? origin : root + 12,
			state->lighting.lights, state->capacity,
			&state->lighting.settings.light_count,
			NULL) != MH_ERROR_NONE)
		return 0;

	return mh_software_model_light_prepare(&state->lighting, model);
}
