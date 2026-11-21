#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/key.h"
#include "engine/assets/track_flag.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "track flag line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

enum {
	BUFFER_BYTES = 65536,
	NAME_BYTES = 256
};

static unsigned char key[BUFFER_BYTES], scratch[BUFFER_BYTES];

static int
encode_name(const char *name)
{
	unsigned char *data;
	size_t got, size;
	enum mh_error_code status;

	status = mh_track_flag_encode(name, &data, &size, NULL);
	CHECK(status == MH_ERROR_NONE);
	got = fwrite(data, 1, size, stdout);
	free(data);

	return got != size || ferror(stdout);
}

int
main(int argc, char *argv[])
{
	unsigned char *data, *other;
	size_t size, other_size;
	int available;
	char long_name[NAME_BYTES + 1];

	if (argc == 3 && strcmp(argv[1], "--encode") == 0)
		return encode_name(argv[2]);

	CHECK(argc == 1);
	CHECK(mh_track_flag_encode("RedRock", &data, &size, NULL) ==
			MH_ERROR_NONE &&
		size == 522);
	CHECK(mh_track_flag_encode("rEDrOCK", &other, &other_size, NULL) ==
			MH_ERROR_NONE &&
		other_size == size);
	CHECK(memcmp(data, other, size) == 0);
	free(other);
	available = 99;
	CHECK(mh_track_flag_validate(data, size, "REDROCK", &available, NULL) ==
			MH_ERROR_NONE &&
		available == 1);
	CHECK(mh_track_flag_validate(NULL, 0, "redrock", &available, NULL) ==
			MH_ERROR_NONE &&
		available == 0);
	CHECK(mh_track_flag_validate(data, size - 1, "RedRock", &available,
		NULL) == MH_ERROR_NONE &&
		available == 0);
	data[size / 2] ^= 128;
	CHECK(mh_track_flag_validate(data, size, "RedRock", &available, NULL) ==
			MH_ERROR_NONE &&
		available == 0);
	free(data);
	memset(long_name, 'x', NAME_BYTES);
	long_name[NAME_BYTES] = 0;
	data = key;
	size = 99;
	available = 99;
	CHECK(mh_track_flag_encode(long_name, &data, &size, NULL) !=
			MH_ERROR_NONE &&
		data == key && size == 99);
	CHECK(mh_track_flag_encode("", &data, &size, NULL) != MH_ERROR_NONE &&
		data == key && size == 99);
	CHECK(mh_track_flag_encode(NULL, &data, &size, NULL) != MH_ERROR_NONE &&
		data == key && size == 99);
	CHECK(mh_track_flag_validate(NULL, 1, "RedRock", &available, NULL) !=
			MH_ERROR_NONE &&
		available == 99);
	CHECK(mh_track_flag_validate(key, 0, "", &available, NULL) !=
			MH_ERROR_NONE &&
		available == 99);
	memset(key, 0xa5, sizeof(key));
	CHECK(mh_key_expand(NULL, 0, key, 3, scratch, sizeof(scratch), NULL) !=
			MH_ERROR_NONE &&
		key[0] == 0xa5);
	CHECK(mh_key_expand(NULL, 0, key, 65537UL, scratch, sizeof(scratch),
		NULL) != MH_ERROR_NONE &&
		key[0] == 0xa5);
	CHECK(mh_key_expand(NULL, 0, key, 233, scratch, 232, NULL) !=
			MH_ERROR_NONE &&
		key[0] == 0xa5);
	CHECK(mh_key_expand((const unsigned char *)"a\0b", 3, key, 233, scratch,
		sizeof(scratch), NULL) != MH_ERROR_NONE &&
		key[0] == 0xa5);
	CHECK(mh_key_expand(NULL, 0, key, 4, scratch, sizeof(scratch), NULL) ==
		MH_ERROR_NONE);
	puts("track flags: case identity, size, corruption, missing data and atomic errors passed");

	return 0;
}
