#include <string.h>

#include "client/race/internal.h"
#include "engine/assets/path_effects.h"
#include "engine/storage/asset_path.h"

/* An absent presentation pack is optional; a present broken pack is not. */
enum mh_error_code
mh_race_load_finish(struct mh_race_loop *loop, struct mh_error *error_out)
{
	struct mh_race_scene *scene;
	char path[1024];
	int found;
	enum mh_error_code status;

	scene = &loop->context->scene;
	loop->finish_count = 0;

	if (scene->assets.finish.motion.keys == NULL) {
		status = mh_asset_path_find_from(loop->context->resources,
			loop->root, MH_ASSET_FINISH_DIR, path, sizeof(path),
			&found, error_out);

		if (status != MH_ERROR_NONE || !found)
			return status;

		status = mh_race_scene_load_finish(
			loop->context->resources, loop->root, scene, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	loop->finish_count = MH_FINISH_SIGNS;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_sign(struct mh_race_loop *loop, struct mh_error *error_out)
{
	struct mh_finish_frame frame;
	float camera[16];
	unsigned int rank, i, column, row;
	enum mh_error_code status;

	rank = 0;

	if (loop->finish_seen && !loop->field->time_attack &&
		mh_field_viewed(loop->field) == 0 &&
		loop->viewed->camera_mode != MH_CAMERA_TRACK) {
		rank = loop->finish_position;

		if (loop->session != NULL &&
			loop->session->finish_position != NULL)
			rank = loop->session->finish_position(
				loop->session->context, rank);
	}

	memset(camera, 0, sizeof(camera));
	camera[15] = 1;

	for (column = 0; column < 3; column++)
		for (row = 0; row < 3; row++)
			camera[column * 4 + row] =
				(float)loop->viewed->camera
					.rotation[column * 3 + row];

	for (row = 0; row < 3; row++)
		camera[12 + row] = (float)loop->viewed->eye[row];

	status = mh_finish_frame(&loop->context->scene.assets.finish, rank,
		loop->effect_dt, camera, &loop->finish_seconds, &frame,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < loop->finish_count; i++)
		if (!mh_renderer_mesh_visible(loop->renderer,
			loop->finish_mesh + i, frame.rank == i + 1))
			return mh_renderer_error(loop->renderer, error_out);

	if (frame.rank == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	memcpy(loop->models + (loop->finish_mesh + frame.rank - 1) * 16,
		frame.model, sizeof(frame.model));

	return mh_race_scene_finish_lighting(&loop->context->scene, loop->field,
		loop->renderer, loop->finish_mesh, &frame, loop->reflections,
		error_out);
}

int
mh_race_prepare_finish(struct mh_race_loop *loop)
{
	if (loop->finish_count == 0)
		return 1;

	if (prepare_sign(loop, &loop->error) != MH_ERROR_NONE) {
		fprintf(stderr, "Finish sign: %s\n", loop->error.message);
		loop->success = loop->running = 0;
		return 0;
	}

	return 1;
}
