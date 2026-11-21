#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "engine/render/software_skid.h"

#define COORDINATE_LIMIT 1000000
#define NORMAL_LIMIT 2
#define END_EXTENSION .02
#define HALF_WIDTH .1
#define SPLIT_LENGTH 2.5

static int
valid_vector(const float values[3], double limit)
{
	unsigned int i;

	for (i = 0; i < 3; i++)
		if (!(values[i] >= -limit && values[i] <= limit))
			return 0;

	return 1;
}

/* Round each step of normalization to float, including the square root. */
static float
normalize(float vector[3])
{
	float squares[3], sum, length, inverse;
	unsigned int i;

	for (i = 0; i < 3; i++)
		squares[i] = (float)((double)vector[i] * vector[i]);

	sum = (float)((double)squares[1] + squares[0]);
	sum = (float)((double)sum + squares[2]);
	length = (float)sqrt((double)sum);

	if (length == 0)
		return 0;

	inverse = (float)(1.0 / length);

	for (i = 0; i < 3; i++)
		vector[i] = (float)((double)vector[i] * inverse);

	return length;
}

enum mh_error_code
mh_software_skid_prepare(const struct mh_skid_segment *segment,
	struct mh_software_skid *output, struct mh_error *error_out)
{
	struct mh_software_skid skid;
	float length, first, second;
	unsigned int i;

	if (segment == NULL || output == NULL ||
		!valid_vector(segment->start, COORDINATE_LIMIT) ||
		!valid_vector(segment->end, COORDINATE_LIMIT) ||
		!valid_vector(segment->normal, NORMAL_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software skid geometry");

	memset(&skid, 0, sizeof(skid));
	memcpy(skid.start, segment->start, sizeof(skid.start));
	memcpy(skid.end, segment->end, sizeof(skid.end));

	for (i = 0; i < 3; i++)
		skid.direction[i] =
			(float)((double)segment->end[i] - segment->start[i]);

	length = normalize(skid.direction);

	if (length == 0) {
		*output = skid;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (i = 0; i < 3; i++) {
		first = (float)((double)segment->normal[(i + 1) % 3] *
			skid.direction[(i + 2) % 3]);
		second = (float)((double)segment->normal[(i + 2) % 3] *
			skid.direction[(i + 1) % 3]);
		skid.lateral[i] = (float)((double)first - second);
		skid.extension[i] =
			(float)((long double)skid.direction[i] * END_EXTENSION);
		first = (float)((double)skid.direction[i] * .5);
		skid.sample[i] = (float)((double)segment->start[i] + first);
	}

	if (normalize(skid.lateral) == 0) {
		*output = skid;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	/*
	 * Binary64 constants must not acquire an extra binary64 product
	 * rounding.
	 */
	for (i = 0; i < 3; i++)
		skid.lateral[i] =
			(float)((long double)skid.lateral[i] * HALF_WIDTH);

	if ((double)length > (UINT_MAX - 2) / 2)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software skid subdivision exceeds index range");

	skid.quads = length > SPLIT_LENGTH ? (unsigned int)length : 1;
	skid.step = (float)((double)length / skid.quads);
	*output = skid;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_software_skid_pair(const struct mh_software_skid *skid, unsigned int station,
	float left[3], float right[3])
{
	float center, distance, offset;
	unsigned int i;

	distance = (float)((double)station * skid->step);

	for (i = 0; i < 3; i++) {
		if (station == 0)
			center = (float)((double)skid->start[i] -
				skid->extension[i]);
		else if (station == skid->quads)
			center = (float)((double)skid->end[i] +
				skid->extension[i]);
		else {
			offset = (float)((double)distance * skid->direction[i]);
			center = (float)((double)skid->start[i] + offset);
		}

		left[i] = (float)((double)center - skid->lateral[i]);
		right[i] = (float)((double)center + skid->lateral[i]);
	}
}
