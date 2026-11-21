#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/assets/ai_path.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/ai_path.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

struct invalid_field {
	unsigned int	offset;
	unsigned long	value;
};

static void
write_word(unsigned char *bytes, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		bytes[i] = (unsigned char)(value >> (8 * i));
}

static void
make_path_bytes(unsigned char data[177])
{
	static const unsigned long x[4] = {
		0, 0x40000000UL, 0x40800000UL, 0x40c00000UL};

	unsigned int i;

	memset(data, 0, 177);
	write_word(data, 4);

	for (i = 0; i < 4; i++) {
		write_word(data + 4 + i * 36, x[i]);
		write_word(data + 4 + i * 36 + 8, 0x40a00000UL);  /* width 5 */
		write_word(data + 4 + i * 36 + 12, 0x40800000UL); /* width 4 */
		write_word(data + 4 + i * 36 + 16,
			0x3f800000UL); /* direction +X */
		write_word(data + 4 + i * 36 + 32, i / 2);
	}

	write_word(data + 148, 2); /* section table */
	write_word(data + 156, 2);
	write_word(data + 164, 1);
	write_word(data + 168, 2);
	write_word(data + 172, 0xc2c80000UL); /* -100 */
}

int
main(void)
{
	static const struct invalid_field invalid_fields[] = {{0, 0}, {0, 1},
		{0, MH_U32_MAX}, {148, 100}, {148, 0}, {4, 0x7f800000UL},
		{8, 0x7fc00000UL}, {12, 0xbf800000UL}, {16, 0x7f7fffffUL},
		{20, 0}, {36, 2}, {152, 1}, {156, 5}, {172, 0xff800000UL}};

	unsigned char data[177];
	struct mh_ai_path path, saved;
	struct mh_ai_path patched;
	struct mh_ai_location location, before;
	unsigned int i;

	make_path_bytes(data);
	CHECK(mh_ai_path_dat_decode(data, 176, &path, NULL) == MH_ERROR_NONE);
	CHECK(path.count == 4 && path.section_count == 2);
	CHECK(path.points[3].x == 6 && path.points[3].section == 1);
	CHECK(path.points[2].width[0] == 5 && path.points[2].width[1] == 4);
	CHECK(path.sections[1].count == 2 && path.sections[1].radius == -100);
	saved = path;
	make_path_bytes(data);
	write_word(data + 156, 0);
	write_word(data + 168, 1);
	memset(&patched, 0, sizeof(patched));
	CHECK(mh_ai_path_dat_decode(data, 176, &patched, NULL) ==
		MH_ERROR_NONE);

	if (patched.sections != NULL && patched.points != NULL) {
		CHECK(patched.sections[0].count == 0 &&
			patched.sections[1].count == 1);
		CHECK(patched.points[1].section == 0 &&
			patched.points[2].section == 1);
	}

	mh_ai_path_free(&patched);
	make_path_bytes(data);

	for (i = 0; i < 176; i++) {
		CHECK(mh_ai_path_dat_decode(data, i, &path, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&path, &saved, sizeof(path)) == 0);
	}

	CHECK(mh_ai_path_dat_decode(data, 177, &path, NULL) != MH_ERROR_NONE);
	CHECK(mh_ai_path_dat_decode(NULL, 176, &path, NULL) != MH_ERROR_NONE);
	CHECK(mh_ai_path_dat_decode(data, 176, NULL, NULL) != MH_ERROR_NONE);

	for (i = 0; i < sizeof(invalid_fields) / sizeof(invalid_fields[0]);
		i++) {
		make_path_bytes(data);
		write_word(data + invalid_fields[i].offset,
			invalid_fields[i].value);
		CHECK(mh_ai_path_dat_decode(data, 176, &path, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&path, &saved, sizeof(path)) == 0);
	}

	CHECK(mh_ai_locate(&path, 3, 0, &location, NULL) == MH_ERROR_NONE);
	CHECK(location.point == 1 && location.distance == 1 &&
		location.in_range);
	CHECK(mh_ai_locate(&path, 6, 0, &location, NULL) == MH_ERROR_NONE);
	CHECK(location.point == 3 && location.distance == 0 &&
		location.in_range);
	CHECK(mh_ai_locate(&path, 672, 0, &location, NULL) == MH_ERROR_NONE);
	CHECK(location.point == 0 && location.distance == 672 &&
		!location.in_range);
	CHECK(mh_ai_locate(&path, 671.5, 0, &location, NULL) == MH_ERROR_NONE);
	CHECK(location.point == 3 && location.distance == 665.5 &&
		location.in_range);
	before = location;
	CHECK(mh_ai_locate(&path, 1e7, 0, &location, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&before, &location, sizeof(before)) == 0);
	CHECK(mh_ai_locate(NULL, 0, 0, &location, NULL) != MH_ERROR_NONE);
	CHECK(mh_ai_locate(&path, 0, 0, NULL, NULL) != MH_ERROR_NONE);
	mh_ai_path_free(&path);
	mh_ai_path_free(&path);
	CHECK(path.points == NULL && path.sections == NULL && path.count == 0);

	if (failures != 0)
		return 1;

	puts("AI disk bounds, stored section metadata, lookup cutoff and atomic failures passed");

	return 0;
}
