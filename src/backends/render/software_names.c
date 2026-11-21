#include <string.h>

#include "backends/render/software_names.h"

enum {
	NAME_FAR_MARGIN = 10
};

enum mh_error_code
mh_software_names_prepare(struct mh_software_names *output,
	const struct mh_renderer_names *names,
	const struct mh_projection *projection, const float view[16],
	struct mh_error *error_out)
{
	struct mh_software_name_source source;
	struct mh_software_depth_band band;
	const struct mh_renderer_name *input;
	struct mh_software_name_entry *entry;
	float inverse_range, camera[12], model_camera[12];
	size_t i;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software name scratch");

	output->count = 0;

	if (names == NULL || names->count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (names->sources == NULL || names->count > MH_NAME_LABELS_MAX ||
		(names->transparent != 0 && names->transparent != 1) ||
		!mh_software_camera_transform(view, NULL, camera))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software name submission");

	status = mh_software_projection_check(projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Name margins are near=0, far=10. */
	inverse_range = (float)(1.0L /
		((long double)projection->far_distance -
			projection->clip.near_distance));
	band.offset = (float)((double)NAME_FAR_MARGIN * inverse_range);
	band.scale = (float)(1.0 - band.offset);
	memset(&source, 0, sizeof(source));
	source.transparent = names->transparent;

	for (i = 0; i < names->count; i++) {
		input = &names->sources[i];
		entry = &output->entries[i];

		if (input->model == NULL ||
			(i != 0 && input->part < names->sources[i - 1].part) ||
			!mh_software_camera_transform(
				view, input->model, model_camera))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software name source or order");

		source.image = input->image;
		source.height = input->height;
		memcpy(source.size, input->size, sizeof(source.size));
		memcpy(source.world, input->model + 12, sizeof(source.world));
		memcpy(source.camera, model_camera + 9, sizeof(source.camera));
		status = mh_software_name_project(&source, projection, camera,
			&band, &entry->geometry, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		entry->image = input->image;
		entry->part = input->part;
	}

	output->count = names->count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
