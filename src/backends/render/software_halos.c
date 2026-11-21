#include <stdlib.h>
#include <string.h>

#include "backends/render/software_halos.h"

static enum mh_error_code
reserve_scratch(struct mh_software_halos *halos, size_t count,
	struct mh_error *error_out)
{
	struct mh_software_halo_entry *entries;
	struct mh_software_halo_marker *markers;
	size_t *draw_order;

	if (count <= halos->capacity)
		return MH_ERROR_NONE;

	if (count > (size_t)-1 / sizeof(*entries) ||
		count > (size_t)-1 / sizeof(*markers) ||
		count > (size_t)-1 / sizeof(*draw_order))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software halo capacity overflow");

	entries = malloc(count * sizeof(*entries));
	markers = malloc(count * sizeof(*markers));
	draw_order = malloc(count * sizeof(*draw_order));

	if (entries == NULL || markers == NULL || draw_order == NULL) {
		free(entries);
		free(markers);
		free(draw_order);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing software halos");
	}

	free(halos->entries);
	free(halos->markers);
	free(halos->draw_order);
	halos->entries = entries;
	halos->markers = markers;
	halos->draw_order = draw_order;
	halos->capacity = count;

	return MH_ERROR_NONE;
}

void
mh_software_halos_free(struct mh_software_halos *halos)
{
	if (halos == NULL)
		return;

	free(halos->entries);
	free(halos->markers);
	free(halos->draw_order);
	memset(halos, 0, sizeof(*halos));
}

static void
rotate_direction(
	const float camera[12], const float direction[3], float output[3])
{
	float x, y, z;
	unsigned int axis;

	/* Three products and two sums, in scene precision. */
	for (axis = 0; axis < 3; axis++) {
		x = (float)((double)direction[0] * camera[axis]);
		y = (float)((double)direction[1] * camera[axis + 3]);
		z = (float)((double)direction[2] * camera[axis + 6]);
		x = (float)((double)y + x);
		output[axis] = (float)((double)x + z);
	}
}

enum mh_error_code
mh_software_halos_prepare(struct mh_software_halos *halos,
	const struct mh_renderer_halos *inputs,
	const struct mh_projection *projection, const float view[16],
	struct mh_error *error_out)
{
	struct mh_software_halo_source source;
	struct mh_software_halo_entry *entry;
	struct mh_software_fog fog;
	struct mh_software_order order;
	const struct mh_renderer_halo *input, *previous;
	const struct mh_software_depth_band *band;
	float camera[12];
	size_t i;
	unsigned int endpoint;
	enum mh_error_code status;

	if (halos == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software halo scratch");

	halos->count = 0;

	if (inputs == NULL || inputs->count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (inputs->sources == NULL || projection == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software halo submission");

	status = mh_software_fog_setup(projection->clip.near_distance,
		projection->far_distance, inputs->cue_start, &fog, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_software_order_setup(projection->clip.near_distance,
			projection->far_distance, MH_SOFTWARE_ORDER_ROOT,
			&order, error_out);

	if (status == MH_ERROR_NONE)
		status = reserve_scratch(halos, inputs->count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < inputs->count; i++) {
		input = &inputs->sources[i];

		if (i != 0) {
			previous = &inputs->sources[i - 1];

			if (input->part < previous->part ||
				(input->part == previous->part &&
					input->before_primitive <
						previous->before_primitive))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"software halo insertion order is invalid");
		}

		if (!mh_software_camera_transform(view, input->model, camera))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software halo transform");

		memset(&source, 0, sizeof(source));
		source.flags = input->flags;
		source.intensity = input->intensity;
		source.size = input->size;
		source.flare = input->flare;
		source.tinted = input->tinted;
		memcpy(source.color, input->color, sizeof(source.color));
		memcpy(source.tint, input->tint, sizeof(source.tint));
		rotate_direction(camera, input->direction, source.direction);

		for (endpoint = 0; endpoint < 2; endpoint++) {
			status = mh_software_project_point(projection, camera,
				input->endpoints[endpoint],
				&source.endpoints[endpoint], error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		/* World: 4/6 margins; attached: 6/4. */
		band = &order.bands[0][source.flags & MH_HALO_WORLD
				? MH_SOFTWARE_ORDER_FLAG4
				: MH_SOFTWARE_ORDER_FLAG128];
		entry = &halos->entries[i];
		memset(entry, 0, sizeof(*entry));
		entry->part = input->part;
		entry->before_primitive = input->before_primitive;
		status = mh_software_halo_geometry(&source, projection, &fog,
			band, input->image, inputs->lens, input->lenses,
			&entry->geometry, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	/*
	 * Retain culled entries so insertion-position validation is
	 * independent.
	 */
	halos->count = inputs->count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
