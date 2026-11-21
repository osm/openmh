#include <string.h>

#include "engine/assets/key.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define MAX_KEY_BYTES 65536UL
#define KEY_BYTE_MASK 255UL

enum {
	MIN_KEY_BYTES = 4,
	MIX_ROUNDS = 4
};

enum mh_error_code
mh_key_expand(const unsigned char *seed, size_t seed_size, unsigned char *key,
	size_t key_size, unsigned char *scratch, size_t scratch_size,
	struct mh_error *error_out)
{
	static const unsigned char fallback[2] = {'E', 'Q'};
	unsigned long mix_state, seed_byte;
	size_t i, prefix_size;

	if (key == NULL || scratch == NULL || key == scratch ||
		key_size < MIN_KEY_BYTES || key_size > MAX_KEY_BYTES ||
		scratch_size < key_size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid key expansion buffers");

	prefix_size = seed_size;

	if (seed == NULL || prefix_size == 0) {
		seed = fallback;
		prefix_size = sizeof(fallback);
	}

	if (prefix_size > key_size / 2 - 1)
		prefix_size = key_size / 2 - 1;

	if (memchr(seed, 0, prefix_size) != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"key seed contains a terminator");

	memcpy(scratch, seed, prefix_size);
	mix_state = (unsigned long)prefix_size;

	for (i = 0; i < MIX_ROUNDS; i++)
		mix_state = (mix_state * mix_state) & MH_U32_MAX;

	for (;;) {
		if (prefix_size > key_size / 2)
			prefix_size = key_size / 2;

		mix_state =
			(mix_state * (unsigned long)prefix_size) & MH_U32_MAX;

		for (i = 0; i < prefix_size; i++) {
			seed_byte = scratch[i];
			mix_state = (mix_state + seed_byte) & MH_U32_MAX;
			mix_state = (mix_state * mix_state + seed_byte) &
				MH_U32_MAX;
			key[i * 2] = (unsigned char)(mix_state & KEY_BYTE_MASK);
			key[i * 2 + 1] = (unsigned char)((mix_state >> 8) &
				KEY_BYTE_MASK);
		}

		if (prefix_size == key_size / 2) {
			if (key_size % 2 != 0)
				key[key_size - 1] = scratch[1];

			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		prefix_size *= 2;
		memcpy(scratch, key, prefix_size);
	}
}
