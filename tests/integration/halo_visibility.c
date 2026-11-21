#include <stdlib.h>
#include <string.h>

#include "backends/image/provider.h"
#include "client/race/race.h"
#include "engine/assets/myo.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/geometry/model.h"
#include "engine/geometry/world.h"
#include "engine/render/scene_matrices.h"

static struct mh_race_context race_context;
static struct mh_field field;

static void
put32(unsigned char *bytes, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++) {
		bytes[i] = (unsigned char)value;
		value >>= 8;
	}
}

/*
 * A parsed, untextured triangle with a plain halo. Its endpoint is
 * deliberately far from the root, to distinguish parent and source culling.
 */
static void
make_model(unsigned char data[300])
{
	static const unsigned int counts[7] = {1, 1, 1, 1, 0, 0, 0};
	static const unsigned int sizes[7] = {12, 16, 16, 12, 0, 0, 0};
	static const unsigned int offsets[7] = {
		124, 136, 152, 168, 180, 180, 180};

	unsigned int i;

	memset(data, 0, 300);
	memcpy(data, "2OYM", 4);

	for (i = 0; i < 7; i++) {
		put32(data + 4 + i * 12, counts[i]);
		put32(data + 8 + i * 12, sizes[i]);
		put32(data + 12 + i * 12, offsets[i]);
	}

	put32(data + 92, 1);
	put32(data + 100, 180);
	put32(data + 104, 0x40200000UL);
	put32(data + 180, 2);
	put32(data + 184, 2);
	put32(data + 188, 0x447a0000UL);
	put32(data + 200, 0x447a0000UL);
	memset(data + 228, 255, 3);
}

static enum mh_error_code
check_visibility(const unsigned char header[92], const unsigned char data[300],
	unsigned int output[4], struct mh_error *error_out)
{
	static const double brightness[3] = {1, 1, 1};
	struct mh_world_model empty;
	struct mh_world world;
	struct mh_model model;
	struct mh_scene_object pose;
	struct mh_scene_instance instance;
	struct mh_scene_transform physical;
	struct mh_av_settings settings;
	float models[MH_FIELD_MESHES * 16], matrix[16], view[16], values[17];
	unsigned int width, height, car, enabled, unavailable, history, i,
		roots;
	size_t source;
	double value;
	enum mh_error_code status;

	width = (unsigned int)mh_read_u32le(header);
	height = (unsigned int)mh_read_u32le(header + 4);

	if (width < 1 || width > 16384 || height < 1 || height > 16384)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "invalid fixture viewport");

	for (i = 0; i < 17; i++) {
		if (!mh_read_f32le(header + 8 + i * 4, &value))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"invalid fixture float");

		values[i] = (float)value;
	}

	if (!(values[0] >= .5f && values[0] <= 10 && values[1] >= 60 &&
		values[1] <= 300))
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "invalid fixture lens");

	car = (unsigned int)mh_read_u32le(header + 76);
	enabled = (unsigned int)mh_read_u32le(header + 80);
	unavailable = (unsigned int)mh_read_u32le(header + 84);
	history = (unsigned int)mh_read_u32le(header + 88);

	if (car > 2 || enabled > 1 || unavailable > 1 || history > 1 ||
		(!car && unavailable))
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "invalid fixture flags");

	memset(&race_context, 0, sizeof(race_context));
	memset(&field, 0, sizeof(field));
	memset(&world, 0, sizeof(world));
	memset(&empty, 0, sizeof(empty));

	/*
	 * Empty decoded world descriptor; only the scenery instance has faces.
	 */
	world.models = &empty;
	world.model_count = 1;
	memset(&pose, 0, sizeof(pose));
	memset(&physical, 0, sizeof(physical));

	for (i = 0; i < 3; i++) {
		pose.color[i] = 1;
		pose.rotation[i * 4] = physical.rotation[i * 4] = 1;

		/*
		 * Start elsewhere, then exercise the retained physics pose
		 * binding.
		 */
		pose.position[i] = values[14 + i] + 10;
		physical.position[i] = values[14 + i];
	}

	memset(&model, 0, sizeof(model));
	status = mh_model_myo_decode(data, 300, &model, error_out);
	instance.model = &model;
	instance.pose = &pose;

	if (status == MH_ERROR_NONE)
		status = car ? mh_mesh_build_native(mh_image_fallback_decoder(),
				&model, NULL,
				&race_context.scene.assets.meshes[1],
				error_out)
			: mh_mesh_build_world_native(
				mh_image_fallback_decoder(), &world,
				NULL, brightness, &instance, 1,
				&race_context.scene.assets.meshes[0],
				error_out);

	if (status == MH_ERROR_NONE && !car)
		status = mh_mesh_animate_objects(
			&race_context.scene.assets.meshes[0], 0, &physical, 1,
			error_out);

	if (status == MH_ERROR_NONE && car == 2)
		status = mh_mesh_build_native(mh_image_fallback_decoder(),
			&model, NULL, &race_context.scene.assets.meshes[2],
			error_out);

	mh_model_free(&model);

	if (status != MH_ERROR_NONE) {
		mh_race_context_free(&race_context);
		return status;
	}

	field.count = field.active_mask = 1;
	field.unavailable_mask = unavailable;
	field.cars[0].car = &race_context.scene.assets.car;
	field.cars[0].track = &race_context.scene.assets.track;
	field.cars[0].camera_goal = 1;
	field.cars[0].camera_focal = values[0];
	field.cars[0].camera_far = values[1];

	for (i = 0; i < 9; i++)
		field.cars[0].camera.rotation[i] = values[2 + i];

	for (i = 0; i < 3; i++) {
		field.cars[0].camera.position[i] = values[11 + i];
		field.cars[0].eye[i] = values[11 + i];
		field.cars[0].vehicle.motion.rotation[i * 4] = 1;
	}

	mh_drive_matrices(
		&field.cars[0], (double)width / height, models, matrix, view);

	for (i = 0; i < 3; i++)
		models[28 + i] = values[14 + i];

	if (car == 2)
		memcpy(models + 32, models + 16, 16 * sizeof(float));

	status = mh_race_scene_visibility(&race_context.scene, &field, models,
		width, height, &roots, error_out);

	if (status != MH_ERROR_NONE) {
		mh_race_context_free(&race_context);
		return status;
	}

	output[3] = car ? roots & 1U
			: !race_context.scene.assets.meshes[0].draw_hidden[0];
	memset(&settings, 0, sizeof(settings));
	settings.halos = enabled;
	settings.lens_flares = 1;
	race_context.settings = &settings;
	race_context.scene.visuals.lamps.mesh.textures =
		(struct mh_image *)calloc(2, sizeof(struct mh_image));

	if (race_context.scene.visuals.lamps.mesh.textures != NULL)
		race_context.scene.visuals.lamps.mesh.texture_count = 2;

	race_context.scene.visuals.lamps.probes =
		(struct mh_depth_probe *)calloc(
			1, sizeof(struct mh_depth_probe));
	race_context.scene.visuals.lamps.visible = (unsigned char *)malloc(1);

	if (race_context.scene.visuals.lamps.mesh.textures == NULL ||
		race_context.scene.visuals.lamps.probes == NULL ||
		race_context.scene.visuals.lamps.visible == NULL)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"fixture allocation failed");

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_prepare_lamps(
			&race_context.scene, &field, error_out);

	source = car == 2 ? 1 : 0;

	if (status == MH_ERROR_NONE) {
		race_context.scene.visuals.lamps.visible[source] =
			(unsigned char)history;
		status = mh_race_scene_update_lamps(&race_context.scene, &field,
			models, matrix, view, width, height,
			race_context.settings, error_out);
	}

	if (status == MH_ERROR_NONE) {
		output[0] = (unsigned int)race_context.scene.visuals.lamps
				.probes[source]
				.active;
		output[1] = race_context.scene.visuals.lamps.mesh
				.vertices[source * 54 + 42]
				.color[0] > 0;
		output[2] = race_context.scene.visuals.lamps.visible[source];
	}

	mh_race_context_free(&race_context);

	return status;
}

int
main(void)
{
	unsigned char header[92], data[300];
	unsigned int output[4], car, enabled, radius, history, unavailable,
		expected;
	struct mh_error error;
	enum mh_error_code status;

	memset(header, 0, sizeof(header));
	make_model(data);
	put32(header, 640);
	put32(header + 4, 480);
	put32(header + 8, 0x3f800000UL);
	put32(header + 12, 0x42f00000UL);
	put32(header + 16, 0x3f800000UL);
	put32(header + 32, 0x3f800000UL);
	put32(header + 48, 0x3f800000UL);
	put32(header + 64, 0xc47a0000UL);
	put32(header + 72, 0x41a00000UL);

	for (car = 0; car < 3; car++)
		for (enabled = 0; enabled < 2; enabled++)
			for (radius = 0; radius < 2; radius++)
				for (history = 0; history < 2; history++)
					for (unavailable = 0;
						unavailable <= (car ? 1U : 0U);
						unavailable++) {
						put32(header + 76, car);
						put32(header + 80, enabled);
						put32(header + 84, unavailable);
						put32(header + 88, history);
						put32(data + 104,
							radius ? 0x44fa0000UL
							: 0x40200000UL);
						status = check_visibility(
							header, data, output,
							&error);
						expected = car != 2 &&
							enabled && radius &&
							!unavailable;

						if (status != MH_ERROR_NONE ||
							output[0] != expected ||
							output[1] !=
								(expected &&
									history) ||
							output[2] != history ||
							output[3] != radius) {
							fprintf(stderr,
								"halo parent/history fixture failed: %s\n",
								status == MH_ERROR_NONE
									? "mismatch"
									: error.message);
							return 1;
						}
					}

	puts("car/scenery parent bounds gate geometry and probes; child lights stay suppressed with history retained");

	return 0;
}
