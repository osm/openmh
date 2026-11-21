#include <string.h>

#include "engine/core/error.h"
#include "engine/render/scene.h"

enum mh_error_code
mh_race_scene_bind_objects(const struct mh_race_scene *scene,
	struct mh_field *field, struct mh_error *error_out)
{
	struct mh_field_object_source objects[MH_FIELD_OBJECTS];
	const struct mh_object_assets *assets;
	const struct mh_object_asset *source;
	struct mh_field_object_source *object;
	unsigned int i;

	if (scene == NULL || scene->assets.objects.count > MH_FIELD_OBJECTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid driving scenery assets");

	assets = &scene->assets.objects;

	for (i = 0; i < assets->count; i++) {
		source = &assets->objects[i];

		if (source->model_index >= assets->model_count ||
			source->scene_index >= assets->scene.object_count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid driving scenery model index");

		object = &objects[i];
		object->body = &source->body;
		object->hull = &assets->models[source->model_index].hull;
		object->scene = &assets->scene.objects[source->scene_index];
		object->scene_index = source->scene_index;
	}

	return mh_field_set_objects(
		field, objects, (unsigned int)assets->count, error_out);
}

enum mh_error_code
mh_race_scene_animate_objects(struct mh_race_scene *scene,
	const struct mh_field *field, struct mh_error *error_out)
{
	struct mh_scene_transform poses[MH_FIELD_OBJECTS];
	unsigned int i;

	if (scene == NULL || field == NULL || field->count < 1 ||
		field->object_count > MH_FIELD_OBJECTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid driving scenery animation");

	for (i = 0; i < field->object_count; i++) {
		poses[i].scene_index = field->objects[i].source.scene_index;
		memcpy(poses[i].position,
			field->objects[i].state.motion.position,
			sizeof(poses[i].position));
		memcpy(poses[i].rotation,
			field->objects[i].state.motion.rotation,
			sizeof(poses[i].rotation));
	}

	return mh_mesh_animate_objects(&scene->assets.meshes[0],
		field->scene_seconds, poses, field->object_count, error_out);
}
