#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/myw.h"
#include "engine/core/binary.h"

/* Synthetic MYW header and one empty grid cell. */
#define WORLD_BYTES 264

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static void
build_empty_world(unsigned char data[WORLD_BYTES])
{
	unsigned int i;

	memset(data, 0, WORLD_BYTES);
	memcpy(data, "!WYM", 4);
	mh_write_u32le(data + 4, WORLD_BYTES);
	mh_write_u32le(data + 8, 1);
	mh_write_u32le(data + 12, 1);
	mh_write_u32le(data + 0x14, 0x3ff00000UL);
	mh_write_u32le(data + 0x1c, 0x3ff00000UL);
	mh_write_u32le(data + 0x58, 240);

	for (i = 0; i < MH_MYW_BLOCK_COUNT; i++)
		mh_write_u32le(data + 0x64 + i * 12, 240);

	mh_write_u32le(data + 240, MH_MYW_NO_MODEL);
	mh_write_u32le(data + 248, MH_MYW_NO_LIGHT);
}

static int
guards(void)
{
	unsigned char data[WORLD_BYTES], rgb[3];
	struct mh_myw world;
	struct mh_directional_light light, saved;
	float color[3], before[3];
	unsigned int i;

	build_empty_world(data);
	data[0x44] = 255;
	data[0x45] = 128;
	data[0x46] = 0;
	mh_write_u32le(data + 0x48, 0xbf000000UL);
	mh_write_u32le(data + 0x4c, 0x40000000UL);
	mh_write_u32le(data + 0x50, MH_U32_SIGN_BIT);
	CHECK(mh_myw_parse(data, sizeof(data), &world, NULL) == MH_ERROR_NONE);
	CHECK(mh_myw_directional(&world, &light));
	CHECK(light.color[0] == 255 && light.color[1] == 128 &&
		light.color[2] == 0);
	CHECK(light.intensity == -.5 && light.direction[0] == 2 &&
		light.direction[2] == 0);
	CHECK(mh_light_directional_color(light.color, (float)light.intensity,
		color, NULL) == MH_ERROR_NONE);
	CHECK(color[0] == -.5f && color[1] < 0 && color[2] == 0);
	saved = light;
	CHECK(!mh_myw_directional(NULL, &light));
	CHECK(!mh_myw_directional(&world, NULL));
	CHECK(memcmp(&light, &saved, sizeof(light)) == 0);

	for (i = 0; i < 4; i++) {
		build_empty_world(data);
		mh_write_u32le(data + 0x48 + i * 4, 0x7f800000UL);
		CHECK(mh_myw_parse(data, sizeof(data), &world, NULL) ==
			MH_ERROR_NONE);
		CHECK(!mh_myw_directional(&world, &light));
		CHECK(memcmp(&light, &saved, sizeof(light)) == 0);
	}

	memcpy(before, color, sizeof(before));
	rgb[0] = 1;
	rgb[1] = 2;
	rgb[2] = 255;
	CHECK(mh_light_directional_color(rgb, FLT_MAX, color, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(color, before, sizeof(color)) == 0);
	CHECK(mh_light_directional_color(NULL, 1, color, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_light_directional_color(rgb, 1, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(color, before, sizeof(color)) == 0);

	return 0;
}

int
main(void)
{
	return guards();
}
