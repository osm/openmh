#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/camera_motion.h"
#include "engine/assets/scene_text.h"
#include "engine/core/error.h"

#define KEY_LIMIT 100000
#define MINIMUM_KEY_BYTES 28.0

enum mh_error_code
mh_camera_mot_parse(const unsigned char *text, size_t size,
	struct mh_camera_motion *output, struct mh_error *error_out)
{
	struct mh_scene_text input;
	struct mh_camera_motion parsed;
	struct mh_camera_key *key;
	double channels[9];
	double metadata[5];
	size_t i;
	unsigned int j;

	if (text == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing camera motion input/output");

	memset(&input, 0, sizeof(input));
	input.text = text;
	input.size = size;

	if (mh_scene_text_next(&input) != 1 ||
		strcmp(input.line, "LWMO") != 0 ||
		mh_scene_text_next(&input) != 1 ||
		strcmp(input.line, "1") != 0 ||
		mh_scene_text_next(&input) != 1 || strcmp(input.line, "9") != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"expected nine-channel LWMO camera version 1");

	if (mh_scene_text_next(&input) != 1 ||
		!mh_scene_text_numbers(input.line, channels, 1) ||
		channels[0] < 1 || channels[0] > KEY_LIMIT ||
		floor(channels[0]) != channels[0] ||
		channels[0] > (double)(size - input.cursor) / MINIMUM_KEY_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid camera key count");

	parsed.count = (size_t)channels[0];

	if (parsed.count > (size_t)-1 / sizeof(*parsed.keys))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"camera keys exceed addressable memory");

	parsed.keys = (struct mh_camera_key *)calloc(
		parsed.count, sizeof(*parsed.keys));

	if (parsed.keys == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading camera keys");

	for (i = 0; i < parsed.count; i++) {
		if (mh_scene_text_next(&input) != 1 ||
			!mh_scene_text_numbers(input.line, channels, 9) ||
			mh_scene_text_next(&input) != 1 ||
			!mh_scene_text_numbers(input.line, metadata, 5)) {
			free(parsed.keys);
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid camera key");
		}

		key = &parsed.keys[i];
		key->frame = (float)metadata[0];
		key->selector = (float)metadata[1];
		key->focal = (float)metadata[2];
		key->far_factor = (float)metadata[3];
		key->distance_cap = (float)metadata[4];

		for (j = 0; j < 3; j++)
			key->anchor[j] = (float)channels[j];

		for (j = 0; j < 6; j++)
			key->parameter[j] = (float)channels[j + 3];
	}

	if (mh_scene_text_next(&input) != 0) {
		free(parsed.keys);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unexpected text after camera keys");
	}

	*output = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
