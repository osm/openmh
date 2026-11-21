#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/assets/myo.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define MUTATION_MULTIPLIER 1664525UL
#define MUTATION_INCREMENT 1013904223UL

static int failures;

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "line %d: %s\n", __LINE__,             \
				#expression);                                  \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
write_word(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)(value & 0xffUL);
	bytes[1] = (unsigned char)((value >> 8) & 0xffUL);
	bytes[2] = (unsigned char)((value >> 16) & 0xffUL);
	bytes[3] = (unsigned char)((value >> 24) & 0xffUL);
}

/*
 * Small synthetic file: one vertex, one type-0 primitive, one texture name.
 * Blocks are deliberately not contiguous or in header order.
 */
static void
prepare_model(unsigned char *data)
{
	int i;

	memset(data, 0, 192);
	memcpy(data, "2OYM", 4);

	for (i = 0; i < 7; i++) {
		write_word(data + 12 + i * 12, 192);
	}

	write_word(data + 4, 1);
	write_word(data + 8, 12);
	write_word(data + 12, 160);
	write_word(data + 16, 1);
	write_word(data + 20, 16);
	write_word(data + 24, 128);
	write_word(data + 76, 1);
	write_word(data + 80, 6);
	write_word(data + 84, 180);
	write_word(data + 100, 192);
	write_word(data + 104,
		0x40200000UL); /* Original object sphere radius 2.5. */
	write_word(data + 160, 0xbf800000UL); /* -1.0 */
	write_word(data + 164, 0x40200000UL); /*  2.5 */
	write_word(data + 168, 0x3e000000UL); /*  0.125 */
	memcpy(data + 180, "x.iff", 6);
}

static void
check_halos(void)
{
	unsigned char data[312], copy[312];
	struct mh_myo model, saved;
	struct mh_geometry_halo halo, old;
	unsigned int i;

	prepare_model(data);
	memset(data + 192, 0xcd, 120);
	write_word(data + 92, 1);
	write_word(data + 192, 2);
	write_word(data + 196, 7);

	for (i = 0; i < 6; i++)
		write_word(data + 200 + i * 4,
			i == 0 ? 0xbf800000UL : 0x3f800000UL);

	data[240] = 19;
	data[241] = 87;
	data[242] = 255;
	memcpy(copy, data, sizeof(copy));
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) == MH_ERROR_NONE);
	saved = model;
	CHECK(mh_myo_halo(&model, 0, &halo));
	old = halo;
	CHECK(halo.resource == 7 && halo.endpoints[0][0] == -1 &&
		halo.endpoints[1][2] == 1 && halo.color[0] == 19 &&
		halo.color[1] == 87 && halo.color[2] == 255);
	CHECK(!mh_myo_halo(&model, 1, &halo) &&
		memcmp(&halo, &old, sizeof(halo)) == 0);
	CHECK(memcmp(data, copy, sizeof(data)) == 0);

	for (i = 192; i < sizeof(data); i++) {
		CHECK(mh_myo_parse(data, i, &model, NULL) != MH_ERROR_NONE &&
			memcmp(&model, &saved, sizeof(model)) == 0);
	}

	for (i = 0; i < 6; i++) {
		memcpy(data, copy, sizeof(data));
		write_word(data + 200 + i * 4, 0x7f800000UL);
		CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) !=
				MH_ERROR_NONE &&
			memcmp(&model, &saved, sizeof(model)) == 0);
		CHECK(!mh_myo_halo(&saved, 0, &halo) &&
			memcmp(&halo, &old, sizeof(halo)) == 0);
	}

	memcpy(data, copy, sizeof(data));
	write_word(data + 192, 3);
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);
}

int
main(void)
{
	unsigned char data[192];
	unsigned char saved[192];
	unsigned char bits[4];
	struct mh_myo model;
	struct mh_myo untouched;
	size_t size;
	double position[3];
	double value;
	unsigned long seed;
	unsigned long offset;
	int i;
	enum mh_error_code status;

	check_halos();
	prepare_model(data);
	memcpy(saved, data, sizeof(data));
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(data, saved, sizeof(data)) == 0);
	CHECK(model.version == 2);
	CHECK(model.radius == 2.5);
	CHECK(model.blocks[MH_MYO_VERTICES].count == 1);
	CHECK(model.primitive_types[0] == 1);
	CHECK(mh_myo_vertex(&model, 0, position));
	CHECK(position[0] == -1.0 && position[1] == 2.5 &&
		position[2] == 0.125);
	CHECK(!mh_myo_vertex(&model, 1, position));
	CHECK(model.minimum[0] == -1.0 && model.maximum[1] == 2.5);
	untouched = model;
	write_word(data + 104, 0x7fc00000UL);
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&model, &untouched, sizeof(model)) == 0);
	write_word(data + 104, 0xbf800000UL);
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&model, &untouched, sizeof(model)) == 0);
	memcpy(data, saved, sizeof(data));

	for (size = 0; size < sizeof(data); size++) {
		CHECK(mh_myo_parse(data, size, &model, NULL) != MH_ERROR_NONE);
	}

	CHECK(mh_myo_parse(NULL, sizeof(data), &model, NULL) != MH_ERROR_NONE);
	CHECK(mh_myo_parse(data, sizeof(data), NULL, NULL) != MH_ERROR_NONE);

	/* Version 1 must ignore the uninitialized version-2-only slots. */
	memcpy(data, "!OYM", 4);
	memset(data + 88, 0xcd, 16);
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) == MH_ERROR_NONE);
	CHECK(model.version == 1 && model.auxiliary_count == 0);

	prepare_model(data);
	data[0] = 'x';
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);
	prepare_model(data);
	write_word(data + 12, 0xfffffff0UL);
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);
	prepare_model(data);
	write_word(data + 8, MH_U32_MAX);
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);
	prepare_model(data);
	write_word(data + 4, 0x80000001UL);
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);
	prepare_model(data);
	data[128] = 10;
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);
	prepare_model(data);
	data[128] = 7; /* Requires 36 bytes, only 16 available. */
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);
	prepare_model(data);
	write_word(data + 16, 2);
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);
	prepare_model(data);
	data[185] = 'x';
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);
	prepare_model(data);
	write_word(data + 92, MH_U32_MAX);
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);
	prepare_model(data);
	write_word(data + 160, 0x7f800000UL);
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) != MH_ERROR_NONE);

	write_word(bits, MH_U32_MAX);
	CHECK(mh_read_u32le(bits) == MH_U32_MAX);
	CHECK(mh_read_u16le(bits) == (unsigned long)MH_U16_MAX);
	CHECK(!mh_read_f32le(bits, &value));
	write_word(bits, 1);
	CHECK(mh_read_f32le(bits, &value) && value == ldexp(1.0, -149));
	write_word(bits, 0x00800000UL);
	CHECK(mh_read_f32le(bits, &value) && value == ldexp(1.0, -126));
	write_word(bits, 0x7f7fffffUL);
	CHECK(mh_read_f32le(bits, &value) && value == ldexp(16777215.0, 104));

	/* Bounded mutation smoke test, especially useful with sanitizers. */
	seed = 17;

	for (i = 0; i < 4000; i++) {
		prepare_model(data);
		seed = (seed * MUTATION_MULTIPLIER + MUTATION_INCREMENT) &
			MH_U32_MAX;
		offset = seed % 189UL;
		write_word(data + offset, seed);
		memset(&model, 0xa5, sizeof(model));
		memcpy(&untouched, &model, sizeof(model));
		status = mh_myo_parse(data, sizeof(data), &model, NULL);

		if (status != MH_ERROR_NONE) {
			CHECK(memcmp(&model, &untouched, sizeof(model)) == 0);
		}
	}

	if (failures != 0) {
		fprintf(stderr, "%d checks failed\n", failures);
		return 1;
	}

	puts("MYO reader: valid files, malformed inputs and binary32 checks passed");

	return 0;
}
