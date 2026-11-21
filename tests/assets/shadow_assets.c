#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/core/error.h"
#include "engine/render/scene_matrices.h"

static struct mh_race_context race_context;
static struct mh_field field;
static struct mh_vertex saved[MH_TRAFFIC_MAX_CARS * 54];

static unsigned int
count_shadow_vertices(void)
{
	const struct mh_mesh *shadow;
	size_t i;
	unsigned int count;

	shadow = &race_context.scene.visuals.shadow_mesh;

	count = 0;

	for (i = 0; i < shadow->vertex_count; i++)
		if (shadow->vertices[i].color[0] != 0)
			count++;

	return count;
}

static enum mh_error_code
check_full_field(
	float *models, const float matrix[16], struct mh_error *error_out)
{
	struct mh_mesh *shadow;
	enum mh_error_code status;
	unsigned int i;

	/*
	 * Duplicate the settled car to isolate roster/camera policy from AI and
	 * frustum changes. Each still has its own rendered body-matrix slot.
	 */
	shadow = &race_context.scene.visuals.shadow_mesh;

	field.count = MH_TRAFFIC_MAX_CARS;
	field.active_mask = 255;

	for (i = 1; i < field.count; i++) {
		field.cars[i] = field.cars[0];
		memcpy(models + (1 + 5 * i) * 16, models + 16,
			16 * sizeof(float));
	}

	status = mh_shadow_mesh_update(
		shadow, &field, models, matrix, field.cars[0].eye, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (count_shadow_vertices() != 432)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"full field did not contain eight shadows");

	memcpy(saved, shadow->vertices, sizeof(saved));
	field.network = 1;
	status = mh_shadow_mesh_update(
		shadow, &field, models, matrix, field.cars[0].eye, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (memcmp(saved, shadow->vertices, sizeof(saved)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"remote field changed shadow geometry");

	for (i = 0; i < field.count; i++) {
		field.active_mask = 255U ^ (1U << i);
		status = mh_shadow_mesh_update(shadow, &field, models, matrix,
			field.cars[0].eye, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (count_shadow_vertices() != 378)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"departure removed the wrong number of shadows");

		field.active_mask = 255;
		field.unavailable_mask = 1U << i;
		status = mh_shadow_mesh_update(shadow, &field, models, matrix,
			field.cars[0].eye, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (count_shadow_vertices() != 378)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"unavailable car affected other shadows");

		field.unavailable_mask = 0;
		field.viewed_entry = i;
		field.cars[i].camera_mode = MH_CAMERA_IN_CAR;
		status = mh_shadow_mesh_update(shadow, &field, models, matrix,
			field.cars[0].eye, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (count_shadow_vertices() != 378)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"cockpit view affected another car's shadow");

		field.unavailable_mask = 1U << ((i + 1) % field.count);
		status = mh_shadow_mesh_update(shadow, &field, models, matrix,
			field.cars[0].eye, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (count_shadow_vertices() != 324)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"combined cockpit and availability gates failed");

		field.unavailable_mask = 0;
		field.cars[i].camera_mode = MH_CAMERA_CHASE;
	}

	field.viewed_entry = 0;
	models[(1 + 5 * (field.count - 1)) * 16 + 13] += 20;
	status = mh_shadow_mesh_update(
		shadow, &field, models, matrix, field.cars[0].eye, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (count_shadow_vertices() != 378)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"airborne remote car did not use its own body pose");

	memcpy(saved, shadow->vertices, sizeof(saved));
	models[(1 + 5 * (field.count - 1)) * 16] = FLT_MAX;

	if (mh_shadow_mesh_update(shadow, &field, models, matrix,
		field.cars[0].eye, NULL) == MH_ERROR_NONE ||
		memcmp(saved, shadow->vertices, sizeof(saved)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"late-car failure partially updated shadows");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(int argc, char *argv[])
{
	struct mh_drive drive;
	struct mh_mesh *shadow;
	struct mh_engine_input controls;
	float models[MH_FIELD_MESHES * 16], matrix[16], view[16];
	struct mh_error error;
	enum mh_error_code status;
	unsigned int car, track, i, vertices;

	if (argc != 4)
		return 2;

	car = (unsigned int)atoi(argv[2]);
	track = (unsigned int)atoi(argv[3]);

	if (car < 1 || car > 10 || track < 1 || track > 8)
		return 2;

	status = mh_race_context_load(
		argv[1], car, track, 0, &race_context, &error);

	if (status == MH_ERROR_NONE &&
		!race_context.scene.assets.car.has_shadow)
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"missing shipped shadow outline");

	if (status == MH_ERROR_NONE)
		status = mh_drive_start(&race_context.scene.assets.car,
			&race_context.scene.assets.world_collision,
			&race_context.scene.assets.materials,
			&race_context.scene.assets.route,
			&race_context.scene.assets.track, 0, 0, 0, &drive,
			&error);

	if (status == MH_ERROR_NONE)
		status = mh_field_start(
			&drive, NULL, 0, NULL, 0, &field, &error);

	memset(&controls, 0, sizeof(controls));

	for (i = 0; i < 120 && status == MH_ERROR_NONE; i++)
		status = mh_field_tick(&field, &controls, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	shadow = &race_context.scene.visuals.shadow_mesh;

	mh_field_matrices(&field, 1.5, models, matrix, view);
	status = mh_shadow_mesh_update(
		shadow, &field, models, matrix, field.cars[0].eye, &error);
	vertices = count_shadow_vertices();

	if (status == MH_ERROR_NONE && vertices != 54)
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"grid shadow did not contain all nine quads");

	if (status != MH_ERROR_NONE)
		goto done;

	memcpy(saved, shadow->vertices, sizeof(saved));
	matrix[0] = FLT_MAX;

	if (mh_shadow_mesh_update(shadow, &field, models, matrix,
		field.cars[0].eye, NULL) == MH_ERROR_NONE ||
		memcmp(saved, shadow->vertices, sizeof(saved)) != 0) {
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"shadow failure was not atomic");
		goto done;
	}

	mh_field_matrices(&field, 1.5, models, matrix, view);

	for (i = 0; i < 4; i++) {
		if (i == 0)
			field.unavailable_mask = 1;

		if (i == 1) {
			field.unavailable_mask = 0;
			field.active_mask = 0;
		}

		if (i == 2) {
			field.active_mask = 1;
			field.cars[0].camera_mode = MH_CAMERA_IN_CAR;
		}

		if (i == 3) {
			field.cars[0].camera_mode = MH_CAMERA_CHASE;
			models[16 + 13] += 20;
		}

		status = mh_shadow_mesh_update(shadow, &field, models, matrix,
			field.cars[0].eye, &error);

		if (status == MH_ERROR_NONE && count_shadow_vertices() != 0)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"ineligible/airborne car retained a shadow");

		if (status != MH_ERROR_NONE)
			goto done;
	}

	models[16 + 13] -= 20;

	/*
	 * Exact clip-plane boundaries discard the whole quad. A constant
	 * in-frustum mapping isolates this from real camera/raster precision.
	 */
	memset(matrix, 0, sizeof(matrix));
	matrix[15] = 1;

	for (i = 0; i < 4; i++) {
		matrix[14] = i == 0 ? 0 : i == 1 ? 1 : .5f;
		matrix[12] = i == 3 ? 2 : 0;
		status = mh_shadow_mesh_update(shadow, &field, models, matrix,
			field.cars[0].eye, &error);

		if (status == MH_ERROR_NONE &&
			count_shadow_vertices() != (i == 2 ? 54U : 0U))
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"shadow clip gate failed");

		if (status != MH_ERROR_NONE)
			goto done;
	}

	matrix[12] = 0;
	status = check_full_field(models, matrix, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	printf("car%u track%u: %u shadow vertices; availability, cockpit, airborne, clip gates, eight-car local/remote fields and atomic failure passed\n",
		car, track, vertices);

done:
	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	mh_race_context_free(&race_context);

	return status != MH_ERROR_NONE;
}
