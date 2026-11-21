#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/material.h"
#include "engine/assets/tbl.h"
#include "engine/core/error.h"
#include "engine/simulation/grip.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)
#define NEAR(a, b) CHECK(fabs((a) - (b)) < 1e-10)

static const char material_text[] =
	"Materials 2\nMaterialName road\nSpinSpeedLimit 4\nXSpinFriction .4\n"
	"XNoSpinFriction .001\nZNoSpinFriction .9\nNoSpinTurnReduce .8\n"
	"ContactAnimID 1\nMaterialName dirt\nBumpy .5\nNoSpinWheelLimit 1500\nSpinWheelLimit 1000\nContactAnimID 3\n";
static const char surface_text[] =
	"Materials 2\nMaterial dirt\nMaterial ROAD\n";

static void
check_materials(void)
{
	struct mh_materials table;
	struct mh_materials saved;
	unsigned char data[1024];
	unsigned char *wrapped;
	size_t wrapped_size;
	size_t i;
	enum mh_error_code status;
	static const char *bad[] = {"Materials 0", "Materials 65",
		"Materials nan", "Materials 1.5", "Materials 2\nMaterialName a",
		"Materials 1\nMaterials 1\nMaterialName a", "MaterialName a",
		"Materials 1\nMaterialName",
		"Materials 2\nMaterialName a\nMaterialName A",
		"Materials 1\nMaterialName a\nBumpy -1",
		"Materials 1\nMaterialName a\nZSpinFriction nan",
		"Materials 1\nMaterialName a\nZSpinFriction 1.1",
		"Materials 1\nMaterialName a\nBumpy 1\nBumpy 2",
		"Materials 1\nBumpy 1\nMaterialName a",
		"Materials 1\nMaterialName a\nBumpy 1 trailing",
		"Materials 1\nMaterialName abcdefghijklmnopqrstuvwxyz123456",
		"Materials 1\nMaterialName a\nSpinRotLimit 1e99"};

	CHECK(mh_materials_mat_srf_decode_list(
		(const unsigned char *)material_text,
		strlen(material_text), 0, &table, NULL) == MH_ERROR_NONE);
	CHECK(table.count == 2 && table.surface_count == 0 &&
		strcmp(table.entries[0].name, "road") == 0);
	NEAR(table.entries[0].spin_speed, 4);
	saved = table;
	CHECK(mh_materials_mat_srf_decode_list(
		(const unsigned char *)material_text,
		strlen(material_text), 2, &table, NULL) != MH_ERROR_NONE);
	CHECK(mh_materials_mat_srf_decode_list(
		(const unsigned char *)material_text, 10, 0, &table,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
	CHECK(mh_materials_mat_srf_decode_list(
		(const unsigned char *)surface_text, strlen(surface_text),
		1, &table, NULL) == MH_ERROR_NONE);
	CHECK(table.count == 2 && table.surface_count == 2 &&
		strcmp(table.entries[0].name, "dirt") == 0);
	NEAR(table.entries[0].spin_speed, 7);
	CHECK(mh_materials_mat_srf_decode((const unsigned char *)material_text,
		strlen(material_text),
		(const unsigned char *)surface_text, strlen(surface_text),
		&table, NULL) == MH_ERROR_NONE);
	CHECK(table.count == 2 && table.surface_count == 2);
	CHECK(strcmp(table.entries[0].name, "dirt") == 0);
	NEAR(table.entries[1].spin_speed, 4);
	NEAR(table.entries[0].z_roll, (double)(float).858086);
	saved = table;

	status = mh_tbl_encode_named((const unsigned char *)surface_text,
		strlen(surface_text), NULL, &wrapped, &wrapped_size, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE)
		return;

	CHECK(mh_materials_mat_srf_decode((const unsigned char *)material_text,
		strlen(material_text), wrapped, wrapped_size, &table,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
	wrapped[3] = 0;
	CHECK(mh_materials_mat_srf_decode((const unsigned char *)material_text,
		strlen(material_text), wrapped, wrapped_size, &table,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
	CHECK(mh_materials_mat_srf_decode((const unsigned char *)material_text,
		strlen(material_text), wrapped, 3, &table,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
	free(wrapped);

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		CHECK(mh_materials_mat_srf_decode((const unsigned char *)bad[i],
			strlen(bad[i]),
			(const unsigned char *)surface_text,
			strlen(surface_text), &table,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
	}

	memset(data, 'x', sizeof(data));
	CHECK(mh_materials_mat_srf_decode(data, sizeof(data),
		(const unsigned char *)surface_text, strlen(surface_text),
		&table, NULL) != MH_ERROR_NONE);

	for (i = 0; i < strlen(material_text); i++) {
		memcpy(data, material_text, strlen(material_text));
		data[i] = 0;
		CHECK(mh_materials_mat_srf_decode(data, strlen(material_text),
			(const unsigned char *)surface_text,
			strlen(surface_text), &table,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
	}

	strcpy((char *)data, "Materials 1\nMaterial absent\n");
	status = mh_materials_mat_srf_decode(
		(const unsigned char *)material_text, strlen(material_text),
		data, strlen((char *)data), &table, NULL);
	CHECK(status == MH_ERROR_NONE);
	CHECK(table.count == 3 && table.surface_count == 1 &&
		strcmp(table.entries[1].name, "road") == 0);
	NEAR(table.entries[0].spin_speed, 7);
	strcpy((char *)data, "Materials 2\nMaterial a\nMaterial A\n");
	CHECK(mh_materials_mat_srf_decode((const unsigned char *)material_text,
		strlen(material_text), data, strlen((char *)data), &table,
		NULL) != MH_ERROR_NONE);
	strcpy((char *)data, "Materials 27\n");
	CHECK(mh_materials_mat_srf_decode((const unsigned char *)material_text,
		strlen(material_text), data, strlen((char *)data), &table,
		NULL) != MH_ERROR_NONE);
}

static void
check_initial_materials(void)
{
	struct mh_materials table;
	struct mh_motion_state motion;
	struct mh_motion_forces forces;
	struct mh_grip_state state;
	struct mh_grip_control control;
	struct mh_grip_prepared prepared;
	struct mh_grip_result finished;
	struct mh_suspension contacts;
	struct mh_car car;
	double position[3] = {0, 0, 0};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	unsigned int i;

	memset(&table, 0, sizeof(table));
	table.count = 1;
	table.entries[0].turn_roll = .5;
	memset(&state, 0, sizeof(state));
	memset(&control, 0, sizeof(control));
	memset(&forces, 0, sizeof(forces));
	memset(&contacts, 0, sizeof(contacts));
	memset(&car, 0, sizeof(car));
	car.has_grip = 1;
	car.brake_force = 1;
	CHECK(mh_motion_reset(position, rotation, &motion, NULL) ==
		MH_ERROR_NONE);
	motion.velocity[0] = 1;
	motion.velocity[2] = 10;

	for (i = 0; i < 4; i++)
		forces.traction[i][0] = 100;

	/* Original initial state: material zero, support zero. */
	CHECK(mh_grip_prepare(&table, &state, &control, &motion, 11, &forces,
		.04, &prepared, NULL) == MH_ERROR_NONE);
	CHECK(prepared.state.phase == 2.495999813079834);
	NEAR(prepared.forces.traction[0][0], 50);

	/* Weight transfer and roll torque still require four support flags. */
	NEAR(prepared.forces.applied[0][1], 0);
	NEAR(prepared.forces.body_torque[2], 0);

	/* A missed final suspension pass publishes -1, not material zero. */
	CHECK(mh_grip_finish(&car, &table, &prepared.state, &control, &motion,
		&contacts, .04, &finished, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 4; i++) {
		CHECK(!finished.state.wheels[i].supported);
		CHECK(finished.state.wheels[i].material == MH_GRIP_NO_MATERIAL);
	}

	CHECK(mh_grip_prepare(&table, &finished.state, &control, &motion, 11,
		&forces, .04, &prepared, NULL) == MH_ERROR_NONE);
	NEAR(prepared.state.phase, finished.state.phase);
	NEAR(prepared.forces.traction[0][0], 100);
}

/*
 * This fixture crosses phase wrap during the wheel loop and combines bumps,
 * sliding steering and rear-wheel shake. Expected values are fixed regression
 * data with binary53 intermediates.
 */
static void
check_preparation_stores(void)
{
	struct mh_materials table;
	struct mh_grip_state state;
	struct mh_grip_control control;
	struct mh_motion_state motion;
	struct mh_motion_forces forces;
	struct mh_grip_prepared result;
	unsigned int wheel;

	memset(&table, 0, sizeof(table));
	memset(&state, 0, sizeof(state));
	memset(&control, 0, sizeof(control));
	memset(&motion, 0, sizeof(motion));
	memset(&forces, 0, sizeof(forces));
	table.count = 1;
	table.entries[0].turn_spin = .87;
	table.entries[0].bumpy = .37;
	state.phase = 803.9;
	state.sliding = 1;
	state.slide_amount = 13.37;
	control.gear = 2;
	control.wheelspin = 1;
	motion.rotation[0] = motion.rotation[4] = motion.rotation[8] = 1;
	motion.velocity[0] = 1.3;
	motion.velocity[1] = -.4;
	motion.velocity[2] = 3.7;

	for (wheel = 0; wheel < 4; wheel++) {
		state.wheels[wheel].supported = 1;
		state.wheels[wheel].normal_y = 1;
		motion.extension[wheel] = 1;
		forces.traction[wheel][0] = 100.1;
		forces.traction[wheel][2] = -725.3;
		forces.applied[wheel][0] = -21.7;
		forces.applied[wheel][1] = -5300.2;
		forces.applied[wheel][2] = 3.1;
	}

	CHECK(mh_grip_prepare(&table, &state, &control, &motion, 3.7, &forces,
		.037, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.state.phase == 1.961092233657837);
	CHECK(result.forces.body_torque[0] == -315.23834228515625);
	CHECK(result.forces.body_torque[1] == 0);
	CHECK(result.forces.body_torque[2] == 4.239147186279297);

	for (wheel = 0; wheel < 4; wheel++) {
		CHECK(result.x_scale[wheel] == .5205684900283813);
		CHECK(result.forces.traction[wheel][0] == 52.10890579223633);
		CHECK(result.forces.traction[wheel][2] == -725.2999877929688);
		CHECK(result.forces.applied[wheel][1] == -5300.2001953125);
		CHECK(result.forces.applied[wheel][2] == 3.0999999046325684);
	}

	CHECK(result.forces.applied[0][0] == -21.700000762939453);
	CHECK(result.forces.applied[1][0] == -21.700000762939453);
	CHECK(result.forces.applied[2][0] == 1032.36767578125);
	CHECK(result.forces.applied[3][0] == 1075.679443359375);
}

/* Fixed expectations for material damping. */
static void
check_grip(void)
{
	struct mh_materials table;
	struct mh_car car;
	struct mh_motion_state motion;
	struct mh_motion_forces forces;
	struct mh_grip_state state;
	struct mh_grip_control control;
	struct mh_suspension contacts;
	struct mh_grip_prepared prepared;
	struct mh_grip_prepared saved_prepared;
	struct mh_grip_result result;
	struct mh_grip_result saved;
	double position[3];
	double rotation[9];
	double expected;
	double scale;
	unsigned int i;

	CHECK(mh_materials_mat_srf_decode((const unsigned char *)material_text,
		strlen(material_text),
		(const unsigned char *)surface_text, strlen(surface_text),
		&table, NULL) == MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
	car.has_grip = 1;
	car.grip_level = 10;
	car.brake_force = .5;
	memset(&state, 0, sizeof(state));
	memset(&control, 0, sizeof(control));
	memset(&contacts, 0, sizeof(contacts));
	memset(&forces, 0, sizeof(forces));
	memset(position, 0, sizeof(position));
	memset(rotation, 0, sizeof(rotation));
	rotation[0] = rotation[4] = rotation[8] = 1;
	CHECK(mh_motion_reset(position, rotation, &motion, NULL) ==
		MH_ERROR_NONE);
	motion.velocity[0] = 1;
	motion.velocity[1] = 2;
	motion.velocity[2] = 10;
	motion.angular_velocity[0] = motion.angular_velocity[1] =
		motion.angular_velocity[2] = .2;
	control.gear = 1;

	for (i = 0; i < 4; i++) {
		state.wheels[i].supported = contacts.wheels[i].supported =
			contacts.wheels[i].contact.hit = 1;
		state.wheels[i].material = contacts.wheels[i].contact.material =
			1;
		state.wheels[i].normal_y =
			contacts.wheels[i].contact.normal[1] = 1;
		forces.traction[i][0] = 100;
	}

	CHECK(mh_grip_prepare(&table, &state, &control, &motion, 10, &forces,
		.1, &prepared, NULL) == MH_ERROR_NONE);
	CHECK(prepared.state.phase == 6.240000247955322);
	CHECK(prepared.forces.body_torque[2] == -1011.9288940429688);
	NEAR(prepared.forces.traction[0][0], 80);

	/* Braking load transfer is asymmetric: -T in front, +T/2 behind. */
	CHECK(mh_grip_prepare(&table, &state, &control, &motion, 11, &forces,
		.1, &prepared, NULL) == MH_ERROR_NONE);
	NEAR(prepared.forces.applied[0][1], -4700);
	NEAR(prepared.forces.applied[2][1], 2350);
	CHECK(mh_grip_prepare(&table, &state, &control, &motion, 9, &forces, .1,
		&prepared, NULL) == MH_ERROR_NONE);
	NEAR(prepared.forces.applied[0][1], 2350);
	NEAR(prepared.forces.applied[2][1], -4700);
	state.sliding = 1;
	state.slide_amount = 20;
	CHECK(mh_grip_prepare(&table, &state, &control, &motion, 10, &forces,
		.1, &prepared, NULL) == MH_ERROR_NONE);
	NEAR(prepared.x_scale[0], (double)(float).9 / 2);
	CHECK(prepared.forces.body_torque[2] == -126.4911117553711);
	state.sliding = 0;
	state.slide_amount = 0;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.velocity[0] == .4869743883609772);
	NEAR(result.velocity[1], 2);
	CHECK(result.velocity[2] == 9.89519214630127);
	CHECK(!result.state.sliding && result.contacts == 4 &&
		result.state.rear_limit == 3000);
	CHECK(mh_grip_finish_tuned(&car, &table, &state, &control, &motion,
		&contacts, 5.5, .1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.velocity[0] == .5010617971420288);

	/*
	 * Banking weakens lateral damping only; Ny=.5 multiplies its base by 8.
	 */
	for (i = 0; i < 4; i++)
		contacts.wheels[i].contact.normal[1] = .5;

	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.velocity[0] == .5995358228683472);
	CHECK(result.velocity[2] == 9.89519214630127);
	motion.velocity[0] = 20;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.state.sliding);
	CHECK(result.velocity[0] == 19.99997901916504);
	control.gear = 0;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(!result.state.sliding && result.state.slide_amount == 0);
	control.gear = 1;
	control.wheelspin = 1;
	control.rpm = 6000;
	contacts.wheels[2].contact.material = 0;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.state.slide_amount == 4.550000190734863);
	NEAR(result.state.rear_limit, 1500);
	CHECK(result.state.tyre_effects.id[0] == 3 &&
		result.state.tyre_effects.id[1] == 1);
	CHECK(result.state.tyre_effects.gain[0] == 1 &&
		result.state.tyre_effects.gain[1] == 1);
	contacts.wheels[3].supported = 0;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	NEAR(result.state.rear_limit, 1000);
	CHECK(result.state.tyre_effects.id[0] == 3 &&
		result.state.tyre_effects.id[1] == 0);

	/*
	 * Falling and rolling in air preserve vertical velocity; +a4 does not.
	 */
	memset(&contacts, 0, sizeof(contacts));
	state.slide_amount = 123;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(!result.state.sliding && result.state.slide_amount == 123 &&
		result.state.rear_limit == 0);
	NEAR(result.velocity[1], 2);
	control.strong_air_damping = 1;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.velocity[1] == 1.0023744106292725);
	memset(&control, 0, sizeof(control));
	control.gear = 1;
	contacts.wheels[0].supported = contacts.wheels[0].contact.hit = 1;
	contacts.wheels[0].contact.material = 1;
	contacts.wheels[0].contact.normal[1] = 1;
	motion.velocity[0] = 0;
	motion.velocity[2] = 3;
	control.braking = .25;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.brake_factor == .6083999872207642);
	control.braking = .5;
	control.heavy_brake = 1;
	motion.velocity[2] = .5;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	NEAR(result.velocity[2], 0);

	/* Equal damping intervals agree at the final binary32 store. */
	control.braking = 0;
	control.heavy_brake = 0;
	motion.velocity[2] = 10;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	expected = result.velocity[2];
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.05, &result, NULL) == MH_ERROR_NONE);
	scale = result.velocity_scale[2];
	CHECK((float)(10 * scale * scale) == expected);

	/* Wheel visuals use the speed before the additional low-speed hold. */
	memset(&contacts, 0, sizeof(contacts));
	motion.velocity[2] = .5;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.wheel_forward == .49949774146080017);
	CHECK(result.velocity[2] == .4884752035140991);
	contacts.wheels[0].supported = contacts.wheels[0].contact.hit = 1;
	contacts.wheels[0].contact.material = 1;
	contacts.wheels[0].contact.normal[1] = 1;

	/*
	 * Original domain handling: negative Ny produces negative X friction;
	 * pow(negative,fraction) returns zero through the default math handler.
	 */
	contacts.wheels[0].contact.normal[1] = -3.6603985790861771e-5;
	motion.velocity[0] = 4;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.velocity[0] == 0 && result.velocity_scale[0] == 0 &&
		result.state.wheels[0].normal_y < 0);
	state = result.state;
	CHECK(mh_grip_prepare(&table, &state, &control, &motion, 10, &forces,
		.1, &prepared, NULL) == MH_ERROR_NONE);

	/*
	 * A shipped track-7 wall reaches this small positive Ny in an eight-car
	 * race. It is neither invalid nor the original exactly-zero fallback.
	 */
	contacts.wheels[0].contact.normal[1] = 3.9458217315768707e-7;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	NEAR(result.velocity_scale[0], pow((double)(float).99999, .1));
	state = result.state;
	CHECK(mh_grip_prepare(&table, &state, &control, &motion, 10, &forces,
		.1, &prepared, NULL) == MH_ERROR_NONE);
	contacts.wheels[0].contact.normal[1] = -3.9458217315768707e-7;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.velocity_scale[0] == 0);

	/*
	 * Smallest disk binary32 subnormal underflows the stored cube to zero
	 * and selects the original reciprocal fallback, even for negative Ny.
	 */
	contacts.wheels[0].contact.normal[1] = 1.4012984643248171e-45;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	NEAR(result.velocity_scale[0], pow((double)(float).99999, .1));
	contacts.wheels[0].contact.normal[1] = -1.4012984643248171e-45;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.velocity[0] == 3.9999959468841553);

	/*
	 * Reciprocal overflow times zero braking reaches original pow's
	 * unordered-base error path, which returns zero rather than NaN.
	 */
	contacts.wheels[0].contact.normal[1] = 1e-15;
	motion.velocity[2] = 10;
	control.braking = 1000;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.velocity_scale[0] == 0 && result.velocity[0] == 0);
	control.braking = 0;
	contacts.wheels[0].contact.normal[1] = 1e-200;
	saved = result;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	contacts.wheels[0].contact.normal[1] = -1;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.velocity[0] == 0);
	saved = result;
	saved_prepared = prepared;
	contacts.wheels[0].contact.normal[1] = -1.1;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	forces.traction[0][0] = DBL_MAX;
	CHECK(mh_grip_prepare(&table, &state, &control, &motion, 10, &forces,
		.1, &prepared, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&prepared, &saved_prepared, sizeof(prepared)) == 0);
	contacts.wheels[0].contact.normal[1] = 1;
	contacts.wheels[0].contact.material = 20;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	contacts.wheels[0].contact.material = 26;
	CHECK(mh_grip_finish(&car, &table, &state, &control, &motion, &contacts,
		.1, &result, NULL) == MH_ERROR_NONE);
	NEAR(result.state.rear_limit, 0);
}

int
main(void)
{
	check_materials();
	check_preparation_stores();
	check_grip();
	check_initial_materials();

	if (failures != 0)
		return 1;

	puts("Material mapping, grip transitions, banking, braking, air damping and failure atomicity passed");

	return 0;
}
