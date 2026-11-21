#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/animation/scene.h"

#define TIME_LIMIT 1e9
#define DEGREES_TO_RADIANS 0.017453292519943295

/* Swap heading/pitch to build this column-major matrix. */
void
mh_scene_apply_channels(struct mh_scene_object *object, const double values[9])
{
	double heading;
	double pitch;
	double bank;
	double ch;
	double sh;
	double cp;
	double sp;
	double cb;
	double sb;
	double *rotation;
	unsigned int i;

	heading = values[3] * DEGREES_TO_RADIANS;
	pitch = values[4] * DEGREES_TO_RADIANS;
	bank = values[5] * DEGREES_TO_RADIANS;
	ch = cos(heading);
	sh = sin(heading);
	cp = cos(pitch);
	sp = sin(pitch);
	cb = cos(bank);
	sb = sin(bank);
	rotation = object->rotation;
	rotation[0] = cb * ch + sb * sp * sh;
	rotation[1] = sb * cp;
	rotation[2] = sb * sp * ch - cb * sh;
	rotation[3] = cb * sp * sh - sb * ch;
	rotation[4] = cb * cp;
	rotation[5] = cb * sp * ch + sb * sh;
	rotation[6] = cp * sh;
	rotation[7] = -sp;
	rotation[8] = cp * ch;

	for (i = 0; i < 3; i++) {
		object->position[i] = values[i];
		object->color[i] = values[6 + i];
	}
}

void
mh_scene_free(struct mh_scene *scene)
{
	size_t i;

	for (i = 0; i < scene->object_count; i++)
		free(scene->objects[i].keys);

	free(scene->objects);
	memset(scene, 0, sizeof(*scene));
}

/*
 * Nine scalar channels, destination-key linear flag, otherwise time-weighted
 * TCB Hermite.
 */
enum mh_error_code
mh_scene_channels(const struct mh_scene_object *object, double seconds,
	double values[9], struct mh_error *error_out)
{
	const struct mh_scene_key *a;
	const struct mh_scene_key *b;
	const struct mh_scene_key *previous;
	const struct mh_scene_key *next;
	size_t low;
	size_t high;
	size_t middle;
	unsigned int channel;
	double u;
	double u2;
	double u3;
	double duration;
	double out_previous;
	double out_current;
	double in_current;
	double in_next;
	double outgoing;
	double incoming;
	double delta;

	if (object == NULL || values == NULL || object->keys == NULL ||
		object->key_count == 0) {
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing scene motion");
	}

	if (!(seconds >= 0 && seconds <= TIME_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid scene time");

	a = object->keys;
	b = a + object->key_count - 1;

	if (object->key_count == 1) {
		memcpy(values, a->value, sizeof(a->value));
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (object->end_behavior == MH_SCENE_REPEAT)
		seconds = fmod(seconds, b->time);

	if (seconds == 0 || seconds >= b->time) {
		if (seconds != 0)
			a = b;

		memcpy(values, a->value, sizeof(a->value));

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	low = 0;
	high = object->key_count - 1;

	while (high - low > 1) {
		middle = low + (high - low) / 2;

		if (object->keys[middle].time <= seconds)
			low = middle;
		else
			high = middle;
	}

	a = &object->keys[low];
	b = a + 1;
	previous = low == 0 ? a : a - 1;
	next = low + 2 == object->key_count ? b : b + 1;
	duration = b->time - a->time;
	u = (seconds - a->time) / duration;
	u2 = u * u;
	u3 = u2 * u;
	out_previous = (1 - a->tension) * (1 + a->continuity) * (1 + a->bias);
	out_current = (1 - a->tension) * (1 - a->continuity) * (1 - a->bias);
	in_current = (1 - b->tension) * (1 - b->continuity) * (1 + b->bias);
	in_next = (1 - b->tension) * (1 + b->continuity) * (1 - b->bias);

	for (channel = 0; channel < 9; channel++) {
		delta = b->value[channel] - a->value[channel];

		if (b->linear) {
			values[channel] = a->value[channel] + delta * u;
		} else {
			outgoing = low == 0
				? 0.5 * (out_previous + out_current) * delta
				: duration / (b->time - previous->time) *
					(out_previous *
							(a->value[channel] -
								previous->value
									[channel]) +
						out_current * delta);

			/*
			 * Duplicate the last key as its next neighbor for
			 * endpoint tangents.
			 */
			incoming = duration / (next->time - a->time) *
				(in_current * delta +
					in_next *
						(next->value[channel] -
							b->value[channel]));
			values[channel] =
				(2 * u3 - 3 * u2 + 1) * a->value[channel] +
				(-2 * u3 + 3 * u2) * b->value[channel] +
				(u3 - 2 * u2 + u) * outgoing +
				(u3 - u2) * incoming;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_scene_sample(const struct mh_scene_object *object, double seconds,
	struct mh_scene_object *sample, struct mh_error *error_out)
{
	double values[9];
	enum mh_error_code status;

	if (sample == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing scene pose output");

	status = mh_scene_channels(object, seconds, values, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*sample = *object;
	mh_scene_apply_channels(sample, values);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
