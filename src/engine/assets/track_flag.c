#include <stdlib.h>
#include <string.h>

#include "engine/assets/key.h"
#include "engine/assets/track_flag.h"
#include "engine/core/error.h"

#define TRACK_NAME_BYTES 256
#define MIN_PAYLOAD_BYTES 4

static enum mh_error_code
prepare_seed(const char *name, unsigned char *seed, size_t *seed_size,
	size_t *payload_size, struct mh_error *error_out)
{
	size_t i, sum;
	unsigned int byte;

	if (name == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing track flag name");

	sum = 0;

	for (i = 0; i < TRACK_NAME_BYTES && name[i] != 0; i++) {
		byte = (unsigned char)name[i];

		if (byte >= 'a' && byte <= 'z')
			byte -= 'a' - 'A';

		seed[i] = (unsigned char)byte;
		sum += byte;
	}

	if (i == 0 || i == TRACK_NAME_BYTES || sum < MIN_PAYLOAD_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid track flag name");

	*seed_size = i;
	*payload_size = sum;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_track_flag_encode(const char *name, unsigned char **data, size_t *size,
	struct mh_error *error_out)
{
	unsigned char seed[TRACK_NAME_BYTES], *result;
	size_t seed_size = 0, payload_size = 0;
	enum mh_error_code status;

	if (data == NULL || size == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing track flag output");

	status = prepare_seed(name, seed, &seed_size, &payload_size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * At most 255*255 bytes, below the expansion's 65536-byte native bound.
	 */
	if (payload_size > (size_t)-1 / 2)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "track flag size overflow");

	/* The second half is temporary expansion scratch. */
	result = (unsigned char *)malloc(payload_size * 2);

	if (result == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory encoding track flag");

	status = mh_key_expand(seed, seed_size, result, payload_size,
		result + payload_size, payload_size, error_out);

	if (status != MH_ERROR_NONE) {
		free(result);
		return status;
	}

	*data = result;
	*size = payload_size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_track_flag_validate(const unsigned char *data, size_t size, const char *name,
	int *available, struct mh_error *error_out)
{
	unsigned char seed[TRACK_NAME_BYTES], *expected;
	size_t seed_size = 0, payload_size = 0;
	enum mh_error_code status;
	int match;

	if (available == NULL || (data == NULL && size != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track flag input");

	status = prepare_seed(name, seed, &seed_size, &payload_size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (data == NULL || size != payload_size) {
		*available = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (payload_size > (size_t)-1 / 2)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "track flag size overflow");

	expected = (unsigned char *)malloc(payload_size * 2);

	if (expected == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory checking track flag");

	status = mh_key_expand(seed, seed_size, expected, payload_size,
		expected + payload_size, payload_size, error_out);
	match = status == MH_ERROR_NONE &&
		memcmp(data, expected, payload_size) == 0;
	free(expected);

	if (status == MH_ERROR_NONE)
		*available = match;

	return status;
}
