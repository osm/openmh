#include <string.h>

#include "engine/render/name_label.h"
#include "engine/render/scene.h"
#include "engine/render/software_scene.h"

#define NAME_LABEL_SIZE .8f

enum {
	NAME_LABEL_HEIGHT = 2
};

enum mh_error_code
mh_race_scene_software_names(const struct mh_race_scene *scene,
	const struct mh_field *field, const float *models,
	unsigned int visible_roots, int enabled,
	struct mh_renderer_name sources[MH_NAME_LABELS_MAX], size_t *count,
	struct mh_error *error_out)
{
	const struct mh_name_images *images;
	const struct mh_drive *viewed;
	struct mh_renderer_name *source;
	unsigned int entry, viewed_entry, mask;
	size_t used;
	int chase;

	if (scene == NULL || field == NULL || models == NULL ||
		sources == NULL || count == NULL || field->count < 1 ||
		field->count > MH_NAME_LABELS_MAX ||
		scene->assets.software.names.count != field->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software driver name frame");

	images = &scene->assets.software.names;
	viewed_entry = mh_field_viewed(field);
	viewed = &field->cars[viewed_entry];
	chase = !viewed->camera_goal &&
		(viewed->camera_mode == MH_CAMERA_CHASE ||
			(viewed->camera_mode == MH_CAMERA_IN_CAR &&
				viewed->camera_roll.external));
	mask = enabled
		? mh_name_label_mask(field->count,
			field->active_mask & images->active_mask, 0,
			viewed_entry, mh_drive_camera_in_car(viewed), chase) &
			visible_roots
		: 0;
	used = 0;

	for (entry = 0; entry < field->count; entry++) {
		if (!(mask & (1U << entry)))
			continue;

		source = &sources[used++];
		memset(source, 0, sizeof(*source));
		source->part = mh_race_scene_software_car_part(entry);
		source->model = models + (1 + entry * MH_CAR_MESHES) * 16;
		source->image = &images->images[entry];
		source->height = NAME_LABEL_HEIGHT;
		source->size[0] = source->size[1] = NAME_LABEL_SIZE;
	}

	*count = used;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
