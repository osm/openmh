#include <stdlib.h>
#include <string.h>

#include "engine/assets/key.h"
#include "engine/assets/packed.h"
#include "engine/assets/tbl.h"
#include "engine/core/error.h"

#define RESOURCE_LIMIT (16UL * 1024 * 1024)
#define BYTE_MASK 255U

enum {
	TBL_SIGNATURE_BYTES = 3,
	TBL_HEADER_BYTES = 4,
	TBL_FLAGS_OFFSET = 3,
	ENCODE_EXTRA_BYTES = TBL_HEADER_BYTES + 1,
	TBL_TEXT_FLAGS = 5,
	TBL_PACKED_FLAGS = 7,
	FIXED_KEY_BYTES = 233,
	NAME_KEY_BYTES = 227
};

enum tbl_mode {
	TBL_DECODE_TEXT,
	TBL_ENCODE_TEXT,
	TBL_DECODE_PACKED
};

static void
encode_payload(const unsigned char *data, size_t payload_size,
	const unsigned char *fixed_key, const unsigned char *name_key,
	unsigned char *result)
{
	unsigned int sum, byte;
	size_t i;

	sum = 0;

	for (i = payload_size; i != 0; i--) {
		byte = data[i - 1];
		result[i + (TBL_HEADER_BYTES - 1)] =
			(unsigned char)((byte + sum) & BYTE_MASK);
		sum = (sum + byte) & BYTE_MASK;
	}

	sum = 0;

	for (i = 0; i < payload_size; i++) {
		byte = result[i + TBL_HEADER_BYTES];
		result[i + TBL_HEADER_BYTES] =
			(unsigned char)((byte + sum +
						fixed_key[i % FIXED_KEY_BYTES] +
						name_key[i % NAME_KEY_BYTES]) &
				BYTE_MASK);
		sum = (sum + byte) & BYTE_MASK;
	}
}

static void
decode_payload(const unsigned char *data, size_t payload_size,
	const unsigned char *fixed_key, const unsigned char *name_key,
	unsigned char *result)
{
	unsigned int sum;
	size_t i;

	sum = 0;

	for (i = 0; i < payload_size; i++) {
		result[i] =
			(unsigned char)((data[i + TBL_HEADER_BYTES] -
						fixed_key[i % FIXED_KEY_BYTES] -
						name_key[i % NAME_KEY_BYTES] -
						sum) &
				BYTE_MASK);
		sum = (sum + result[i]) & BYTE_MASK;
	}

	sum = 0;

	for (i = payload_size; i != 0; i--) {
		result[i - 1] =
			(unsigned char)((result[i - 1] - sum) & BYTE_MASK);
		sum = (sum + result[i - 1]) & BYTE_MASK;
	}
}

static enum mh_error_code
transform_tbl(const unsigned char *data, size_t size, const char *basename,
	enum tbl_mode mode, unsigned char **output, size_t *output_size,
	struct mh_error *error_out)
{
	static const unsigned char fixed_seed[] = {
		1, 3, 250, 21, 32, 250, 42, 128, 170};

	unsigned char fixed_key[FIXED_KEY_BYTES];
	unsigned char name_key[NAME_KEY_BYTES];
	unsigned char scratch[FIXED_KEY_BYTES];
	unsigned char *result;
	enum mh_error_code status;
	size_t payload_size, basename_size;
	int encode;

	encode = mode == TBL_ENCODE_TEXT;

	if (data == NULL || output == NULL || output_size == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid TBL input");

	if (!encode &&
		(size < TBL_HEADER_BYTES ||
			memcmp(data, "TBL", TBL_SIGNATURE_BYTES) != 0)) {
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing TBL header");
	}

	if (!encode &&
		data[TBL_FLAGS_OFFSET] !=
			(mode == TBL_DECODE_PACKED ? TBL_PACKED_FLAGS
						: TBL_TEXT_FLAGS)) {
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported TBL flags");
	}

	if (encode && size > (size_t)-1 - ENCODE_EXTRA_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "TBL input too large");

	payload_size = encode ? size : size - TBL_HEADER_BYTES;
	result = (unsigned char *)malloc(
		payload_size + (encode ? ENCODE_EXTRA_BYTES : 1));

	if (result == NULL) {
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding TBL");
	}

	status = mh_key_expand(fixed_seed, sizeof(fixed_seed), fixed_key,
		sizeof(fixed_key), scratch, sizeof(scratch), error_out);
	basename_size = basename == NULL ? 0 : strlen(basename);

	if (status == MH_ERROR_NONE)
		status = mh_key_expand((const unsigned char *)basename,
			basename_size, name_key, sizeof(name_key), scratch,
			sizeof(scratch), error_out);

	if (status != MH_ERROR_NONE) {
		free(result);
		return status;
	}

	if (encode) {
		memcpy(result, "TBL", TBL_SIGNATURE_BYTES);
		result[TBL_FLAGS_OFFSET] = TBL_TEXT_FLAGS;
		encode_payload(data, payload_size, fixed_key, name_key, result);

		result[payload_size + TBL_HEADER_BYTES] = 0;
		*output = result;
		*output_size = payload_size + TBL_HEADER_BYTES;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	decode_payload(data, payload_size, fixed_key, name_key, result);

	result[payload_size] = 0;
	*output = result;
	*output_size = payload_size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_tbl_decode(const unsigned char *data, size_t size, unsigned char **text,
	size_t *text_size, struct mh_error *error_out)
{
	return transform_tbl(
		data, size, NULL, TBL_DECODE_TEXT, text, text_size, error_out);
}

enum mh_error_code
mh_tbl_decode_named(const unsigned char *data, size_t size,
	const char *basename, unsigned char **text, size_t *text_size,
	struct mh_error *error_out)
{
	return transform_tbl(data, size, basename, TBL_DECODE_TEXT, text,
		text_size, error_out);
}

enum mh_error_code
mh_tbl_encode_named(const unsigned char *text, size_t size,
	const char *basename, unsigned char **data, size_t *data_size,
	struct mh_error *error_out)
{
	return transform_tbl(text, size, basename, TBL_ENCODE_TEXT, data,
		data_size, error_out);
}

enum mh_error_code
mh_tbl_resource_decode(const unsigned char *data, size_t size,
	const char *basename, unsigned char **output, size_t *output_size,
	struct mh_error *error_out)
{
	unsigned char *decrypted;
	size_t payload_size;
	enum mh_error_code status;

	if (data == NULL || output == NULL || output_size == NULL ||
		size < TBL_HEADER_BYTES || size > RESOURCE_LIMIT ||
		memcmp(data, "TBL", TBL_SIGNATURE_BYTES) != 0 ||
		(data[TBL_FLAGS_OFFSET] != TBL_TEXT_FLAGS &&
			data[TBL_FLAGS_OFFSET] != TBL_PACKED_FLAGS))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported TBL resource header");

	if (data[TBL_FLAGS_OFFSET] == TBL_TEXT_FLAGS)
		return transform_tbl(data, size, basename, TBL_DECODE_TEXT,
			output, output_size, error_out);

	status = transform_tbl(data, size, basename, TBL_DECODE_PACKED,
		&decrypted, &payload_size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_packed_decode(
		decrypted, payload_size, output, output_size, error_out);
	free(decrypted);

	return status;
}
