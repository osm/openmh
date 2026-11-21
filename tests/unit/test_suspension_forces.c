#include <stdio.h>
#include <string.h>

#include "engine/simulation/suspension_internal.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
test_forces(void)
{
	struct mh_car car;
	struct mh_suspension_config config;
	struct mh_suspension_input input;
	struct mh_suspension_wheel wheel;
	double rotation[9] = {.6, .8, 0, -.8, .6, 0, 0, 0, 1};
	double force[3] = {.123, -.456, .789};
	double torque[3] = {.123, -.456, .789};
	double expected_force[3] = {7.859028339385986328125, 22103.68359375,
		10.98392772674560546875};

	double expected_torque[3] = {
		-5529.7353515625, 2.75237274169921875, -2759.3271484375};

	unsigned int i;

	memset(&car, 0, sizeof(car));
	memset(&input, 0, sizeof(input));
	memcpy(input.rotation, rotation, sizeof(rotation));
	car.center_mass[0] = .125;
	car.center_mass[1] = .375;
	car.center_mass[2] = -.25;
	config.length = .55;
	config.strength = 123456.7;
	config.damping = 65000;

	/*
	 * Wheel-force stores with binary53 intermediates, including seeded
	 * accumulators.
	 */
	for (i = 0; i < 4; i++) {
		car.wheel_position[i][0] = i == 0 || i == 3 ? -.5 : .5;
		car.wheel_position[i][2] = i < 2 ? .75 : -.75;
		input.previous_extension[i] = .25;
		input.traction[i][0] = 8;
		input.traction[i][1] = 4;
		input.traction[i][2] = -6;
		input.applied[i][0] = 1.25;
		input.applied[i][1] = -2.75;
		input.applied[i][2] = 3.5;
		memset(&wheel, 0, sizeof(wheel));
		wheel.contact.hit = 1;
		wheel.contact.fraction = .7123456;
		wheel.contact.normal[1] = .8;
		wheel.contact.normal[2] = .6;
		mh_suspension_apply_contact(&car, &config, &input, i, &wheel);
		CHECK(wheel.supported);
		CHECK(wheel.extension == .42469120025634765625);
		CHECK(wheel.force[0] == 1.934007167816162109375);
		CHECK(wheel.force[1] == 5526.03466796875);
		CHECK(wheel.force[2] == 2.54873180389404296875);
		mh_suspension_accumulate(&wheel, force, torque);
	}

	CHECK(memcmp(force, expected_force, sizeof(force)) == 0);
	CHECK(memcmp(torque, expected_torque, sizeof(torque)) == 0);

	/*
	 * The support gate also reads the stored fraction, before projection.
	 */
	wheel.contact.fraction = .50000001;
	mh_suspension_apply_contact(&car, &config, &input, 0, &wheel);
	CHECK(!wheel.supported && wheel.extension == .5);
	CHECK(wheel.spring_force == 0 && wheel.damper_force == 0);
	CHECK(wheel.traction_scale == 0);
	CHECK(wheel.force[0] == 1.25 && wheel.force[1] == -2.75 &&
		wheel.force[2] == 3.5);
}

int
main(void)
{
	test_forces();

	return failures != 0;
}
