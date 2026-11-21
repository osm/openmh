#include "engine/simulation/suspension_internal.h"

#define SUPPORT_FRACTION .5
#define COMPRESSION_BIAS .1

/* Stored wheel force, before accumulator/arm processing. */
static void
apply_support(const struct mh_suspension_config *config,
	const struct mh_suspension_input *input, unsigned int index,
	struct mh_suspension_wheel *wheel)
{
	double normal[3], traction[3];
	double compression, normal_squared, normal_cubed;
	double polynomial, projection, weight, spring, damper;
	volatile double value;
	unsigned int axis;

	if (!wheel->contact.hit)
		return;

	wheel->extension = SUPPORT_FRACTION;

	if (!((double)(float)wheel->contact.fraction > SUPPORT_FRACTION))
		return;

	wheel->supported = 1;
	value = (double)(float)wheel->contact.fraction - SUPPORT_FRACTION;
	value *= 2;
	wheel->extension = (float)value;

	if (wheel->extension > 1)
		wheel->extension = 1;

	compression = (float)(1 - wheel->extension);
	value = (double)(float)wheel->contact.normal[1] *
		(double)(float)wheel->contact.normal[1];
	normal_squared = (float)value;

	/* Compute the cube from the unrounded square. */
	value *= (double)(float)wheel->contact.normal[1];
	normal_cubed = (float)value;
	value = compression + COMPRESSION_BIAS;
	value *= (double)(float)config->strength;
	value *= compression;
	polynomial = (float)value;

	for (axis = 0; axis < 3; axis++) {
		value = (double)(float)wheel->contact.normal[1] *
			(double)(float)input->rotation[3 * axis + 1];
		value += (double)(float)wheel->contact.normal[0] *
			(double)(float)input->rotation[3 * axis];
		value += (double)(float)wheel->contact.normal[2] *
			(double)(float)input->rotation[3 * axis + 2];
		normal[axis] = (float)value;
		traction[axis] = (float)input->traction[index][axis];
	}

	value = normal_cubed * normal[1];
	value *= normal[1];
	weight = value;
	wheel->traction_scale = weight;
	value = traction[1] * normal[1];
	value += traction[0] * normal[0];
	value += traction[2] * normal[2];
	projection = value;
	value = (double)(float)input->previous_extension[index] -
		wheel->extension;
	value *= (double)(float)config->damping;
	damper = value;
	wheel->damper_force = damper;
	value = -(double)(float)config->length * polynomial;
	value *= normal_squared;
	spring = value;
	wheel->spring_force = -spring;

	for (axis = 0; axis < 3; axis++) {
		value = projection * normal[axis];
		value = traction[axis] - value;
		value *= weight;

		/* Add damping, subtract spring, then add applied Y. */
		if (axis == 1) {
			value += damper;
			value -= spring;
		}

		value += (double)(float)input->applied[index][axis];
		wheel->force[axis] = (float)value;
	}
}

void
mh_suspension_apply_contact(const struct mh_car *car,
	const struct mh_suspension_config *config,
	const struct mh_suspension_input *input, unsigned int index,
	struct mh_suspension_wheel *wheel)
{
	double arm[3];
	volatile double value;
	unsigned int axis, j, k;

	wheel->supported = 0;
	wheel->extension = 1;
	wheel->spring_force = wheel->damper_force = wheel->traction_scale = 0;

	for (axis = 0; axis < 3; axis++) {
		wheel->force[axis] = (float)input->applied[index][axis];
		arm[axis] = (float)((double)(float)
					car->wheel_position[index][axis] -
			(double)(float)car->center_mass[axis]);
	}

	apply_support(config, input, index, wheel);

	/* Keep each cross product unrounded until accumulation. */
	for (axis = 0; axis < 3; axis++) {
		j = (axis + 1) % 3;
		k = (axis + 2) % 3;
		value = arm[j] * wheel->force[k];
		value -= arm[k] * wheel->force[j];
		wheel->torque[axis] = value;
	}
}

void
mh_suspension_accumulate(const struct mh_suspension_wheel *wheel,
	double force[3], double torque[3])
{
	volatile double value;
	unsigned int axis;

	/* Store after EACH wheel, including body seeds. */
	for (axis = 0; axis < 3; axis++) {
		value = (double)(float)force[axis] + wheel->force[axis];
		force[axis] = (float)value;
		value = wheel->torque[axis] + (double)(float)torque[axis];
		torque[axis] = (float)value;
	}
}
