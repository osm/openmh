#include <stdio.h>
#include <string.h>

#include "engine/simulation/motion_internal.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
test_translation(void)
{
	struct mh_motion_state state, expected;
	double local[3] = {.013, .000003, -.025};
	unsigned int i;

	memset(&state, 0, sizeof(state));
	state.rotation[0] = state.rotation[8] = .6;
	state.rotation[2] = -.8;
	state.rotation[6] = .8;
	state.rotation[4] = 1;
	state.position[0] = -512.17;
	state.position[1] = 1.677;
	state.position[2] = 4096.3;

	for (i = 0; i < 3; i++) {
		state.velocity[i] = i + 1;
		state.angular_velocity[i] = i + 2;
		state.previous_velocity[i] = i + 3;
		state.previous_angular_velocity[i] = i + 4;
	}

	for (i = 0; i < 4; i++)
		state.extension[i] = .25 * i;

	expected = state;

	/* Translation stores with binary53 intermediates. */
	expected.position[0] = -512.18218994140625;
	expected.position[1] = 1.67700302600860595703125;
	expected.position[2] = 4096.2744140625;
	mh_motion_translate(&state, local);
	CHECK(memcmp(&state, &expected, sizeof(state)) == 0);
}

static void
test_acceleration(void)
{
	struct mh_motion_state state, expected;
	double force[3] = {12345.6, -7654.3, 2345.6};
	double torque[3] = {4567.8, -9876.5, 3456.7};
	double zero[3] = {0, 0, 0};
	double lateral[3] = {
		.6999999284744263, .699999988079071, .7000000476837158};

	double forward[3] = {2.999999761581421, 3, -3};
	unsigned int i, j, sign;

	memset(&state, 0, sizeof(state));
	state.velocity[0] = 12.345;
	state.velocity[1] = -7.89;
	state.velocity[2] = 45.67;
	state.angular_velocity[0] = .123;
	state.angular_velocity[1] = -.456;
	state.angular_velocity[2] = .789;
	state.position[0] = 101;
	state.previous_velocity[1] = 7;
	state.previous_angular_velocity[2] = 3;
	state.extension[0] = .5;
	expected = state;

	/* Integration stores with binary53 intermediates, mass=1103.7f. */
	expected.velocity[0] = 12.400928497314453125;
	expected.velocity[1] = -7.924675464630126953125;
	expected.velocity[2] = 45.68062591552734375;
	expected.angular_velocity[0] = .12952543795108795166015625;
	expected.angular_velocity[1] = -.480691254138946533203125;
	expected.angular_velocity[2] = .800522327423095703125;
	mh_motion_accelerate(&state, 1103.7, force, torque, .005);
	CHECK(memcmp(&state, &expected, sizeof(state)) == 0);

	/* Stored .7f is below the original binary64 .7 comparison constant. */
	for (sign = 0; sign < 2; sign++) {
		for (i = 0; i < 3; i++) {
			for (j = 0; j < 3; j++) {
				memset(&state, 0, sizeof(state));
				state.velocity[0] =
					sign ? -lateral[i] : lateral[i];
				state.velocity[2] = forward[j];
				expected = state;

				if (i < 2 && j == 0)
					expected.velocity[0] = 0;

				mh_motion_accelerate(
					&state, 1100, zero, zero, .005);
				CHECK(memcmp(&state, &expected,
					sizeof(state)) == 0);
			}
		}
	}
}

static void
test_rotation(void)
{
	double rotation[9] = {1, -1.5851792767307415e-8, 0, 0, 1, 0, 0, 0, 1};
	double angle[3] = {0, 0, -1.0567862140931084e-8};
	double expected[9] = {1, -2.641965579641692e-8, 0,
		1.0567862140931084e-8, 1, 0, 0, 0, 1};

	/*
	 * Extended precision matters: double sine/cosine loses a rounding bit.
	 */
	mh_motion_rotate(rotation, angle);
	CHECK(memcmp(rotation, expected, sizeof(rotation)) == 0);
}

static void
test_history(void)
{
	struct mh_motion_state state, expected;
	double rotation[9] = {1, 6.749901513103396e-5, 4.500160866882652e-5,
		-6.750002648914233e-5, 1, 2.249700810352806e-5,
		-4.500009163166396e-5, -2.250004581583198e-5, 1};

	memset(&state, 0, sizeof(state));
	state.rotation[0] = state.rotation[4] = state.rotation[8] = 1;
	state.position[0] = -512.17;
	state.position[1] = 1.677;
	state.position[2] = 4096.3;
	state.velocity[0] = 12.345;
	state.velocity[1] = -7.89;
	state.velocity[2] = 45.67;
	state.angular_velocity[0] = .123;
	state.angular_velocity[1] = -.456;
	state.angular_velocity[2] = .789;
	state.previous_velocity[0] = 12.3;
	state.previous_velocity[1] = -8;
	state.previous_velocity[2] = 45;
	state.previous_angular_velocity[0] = .12;
	state.previous_angular_velocity[1] = -.45;
	state.previous_angular_velocity[2] = .78;
	state.extension[0] = .25;
	expected = state;

	/* History saves precede the velocity rebase. */
	memcpy(expected.rotation, rotation, sizeof(rotation));
	expected.position[0] = -512.16961669921875;
	expected.position[1] = 1.67782497406005859375;
	expected.position[2] = 4096.3046875;
	expected.velocity[0] = 12.346523284912109375;
	expected.velocity[1] = -7.88980579376220703125;
	expected.velocity[2] = 45.669620513916015625;
	expected.previous_velocity[0] = (float)12.345;
	expected.previous_velocity[1] = (float)-7.89;
	expected.previous_velocity[2] = (float)45.67;
	expected.previous_angular_velocity[0] = (float).123;
	expected.previous_angular_velocity[1] = (float)-.456;
	expected.previous_angular_velocity[2] = (float).789;
	mh_motion_correct_history(&state, .015);
	CHECK(memcmp(&state, &expected, sizeof(state)) == 0);
}

static void
test_wrapper(void)
{
	struct mh_motion_state state, saved;
	double position[3], velocity[3];
	double pose[12] = {0.6000000238418579, 0.800000011920929, 0.0,
		-0.8000000715255737, 0.6000000238418579, 1.0000000116860974e-07,
		7.999999951380232e-08, -5.99999978589949e-08, 1.0,
		-0.25599998235702515, -0.19199998676776886, 4096.2998046875};

	double display[12] = {0.6000000238418579, 0.800000011920929, 0.0,
		-0.800000011920929, 0.6000000238418579, 9.999999406318238e-08,
		7.999999240837496e-08, -5.99999978589949e-08, 1.0,
		-0.5120000243186951, 1.6570091077028337e-08, 4096.2998046875};

	double entry[3] = {
		-0.25600001215934753, -0.19199998676776886, 4096.2998046875};

	unsigned int i;

	memset(&state, 0, sizeof(state));
	memcpy(state.rotation, pose, sizeof(state.rotation));
	memcpy(state.position, pose + 9, sizeof(state.position));
	state.velocity[2] = 12;
	state.previous_velocity[1] = 3;
	state.angular_velocity[0] = .125;
	state.extension[0] = .25;
	saved = state;

	/*
	 * Local-Y translation by +.32f, normalization, then translation by
	 * -.32f, with binary53 intermediates.
	 */
	mh_motion_leave(&state);
	CHECK(memcmp(state.rotation, display, sizeof(state.rotation)) == 0);
	CHECK(mh_motion_display(&state, position, velocity, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 3; i++)
		CHECK((float)position[i] == display[9 + i]);

	/* Retain the display rounding, but delay the next internal store. */
	CHECK(state.position[0] != (double)(float)state.position[0]);
	mh_motion_enter(&state);
	CHECK(memcmp(state.position, entry, sizeof(entry)) == 0);
	memcpy(saved.rotation, state.rotation, sizeof(state.rotation));
	memcpy(saved.position, state.position, sizeof(state.position));
	CHECK(memcmp(&state, &saved, sizeof(state)) == 0);
}

static void
test_normalization_fallback(void)
{
	double rotation[9] = {0, 0, 0, 0, 0, 1, 0, 0, 0};
	double expected[9] = {1, 0, 0, -0.0, 0, 1, 0, -1, 0};
	unsigned int i;

	/* A zero X axis is replaced before crossing with the original Y. */
	mh_motion_normalize(rotation);
	CHECK(memcmp(rotation, expected, sizeof(rotation)) == 0);
	memset(rotation, 0, sizeof(rotation));
	rotation[0] = 1;
	rotation[3] = 2;
	mh_motion_normalize(rotation);

	for (i = 0; i < 9; i++)
		CHECK(rotation[i] == (i % 4 == 0 ? 1 : 0));
}

static void
test_motion_passes(void)
{
	struct mh_car car;
	struct mh_collision col;
	struct mh_motion_state state;
	struct mh_motion_forces forces;
	struct mh_motion_result result;
	double position[3] = {0, 1000, 4096};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

	memset(&car, 0, sizeof(car));
	memset(&col, 0, sizeof(col));
	memset(&forces, 0, sizeof(forces));
	car.mass = 1000;
	car.has_suspension = 1;
	car.spring_length = .5;
	CHECK(mh_motion_reset(position, rotation, &state, NULL) ==
		MH_ERROR_NONE);
	state.velocity[2] = state.previous_velocity[2] = 5;
	CHECK(mh_motion_step(&car, &col, &state, &forces, .03, &result, NULL) ==
		MH_ERROR_NONE);

	/* Each .05 local-Z translation stores 4096 + n*.0498046875. */
	CHECK(result.passes[0].wheels[0].start[2] == 4096);
	CHECK(result.passes[1].wheels[0].start[2] == 4096.0498046875);
	CHECK(result.passes[2].wheels[0].start[2] == 4096.099609375);
	CHECK(result.state.position[2] == 4096.1494140625);
	CHECK(result.state.velocity[2] == 5);
	CHECK(result.state.previous_velocity[2] == 5);

	/* Entry history correction must use the same translation stores. */
	state.velocity[2] = 0;
	state.previous_velocity[2] = -10;
	CHECK(mh_motion_step(&car, &col, &state, &forces, .01, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.passes[0].wheels[0].start[2] == 4096.0498046875);
	CHECK(result.state.position[2] == 4096.0498046875);
}

int
main(void)
{
	test_translation();
	test_acceleration();
	test_rotation();
	test_history();
	test_wrapper();
	test_normalization_fallback();
	test_motion_passes();

	return failures != 0;
}
