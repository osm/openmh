#include <stdio.h>
#include <string.h>

#include "engine/assets/palette.h"
#include "engine/assets/remap.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/image/atlas_map.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Resource line %d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static unsigned char remap_bytes[78];

static void
write_word(unsigned int offset, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		remap_bytes[offset + i] = (unsigned char)(value >> (i * 8));
}

static void
prepare_remap(void)
{
	memset(remap_bytes, 0, sizeof(remap_bytes));
	write_word(0, 1);
	write_word(4, 2);
	write_word(8, 1);
	write_word(12, 256);
	write_word(16, 128);
	write_word(20, 0x3f000000UL);
	write_word(24, 0x3f800000UL);
	memcpy(remap_bytes + 28, "image.iff", 10);
	memset(remap_bytes + 38, 0xa3, 3);
	write_word(41, 1);
	write_word(45, 0xa3a3a3a3UL);
	write_word(49, 224);
	write_word(53, 0);
	write_word(57, 1);
	write_word(61, 256);
	write_word(65, 32);
	write_word(69, 128);
	write_word(73, 32);
}

static int
check_remaps(void)
{
	struct mh_atlas_map remap, saved;
	struct mh_atlas_map_entry entry, saved_entry;
	unsigned char original[78];
	unsigned int i;

	prepare_remap();
	memcpy(original, remap_bytes, sizeof(remap_bytes));
	CHECK(mh_atlas_remap_decode(remap_bytes, 77, &remap, NULL) ==
		MH_ERROR_NONE);
	CHECK(remap.count == 1 && remap.pages == 2 && remap.cutout_pages == 1);
	CHECK(remap.width == 256 && remap.height == 128 &&
		remap.scale[0] == .5 && remap.scale[1] == 1);
	CHECK(mh_atlas_map_entry(&remap, 0, &entry, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(entry.name, "image.iff") == 0 &&
		(unsigned char)entry.name[12] == 0xa3);
	CHECK(entry.page == 1 && entry.saved_handle == 0xa3a3a3a3UL &&
		entry.transpose == 1);
	CHECK(entry.x == 224 && entry.y == 0 && entry.source_width == 256 &&
		entry.source_height == 32);
	CHECK(entry.scaled_width == 128 && entry.scaled_height == 32);
	CHECK(memcmp(original, remap_bytes, sizeof(remap_bytes)) == 0);
	saved = remap;
	saved_entry = entry;
	CHECK(mh_atlas_map_entry(&remap, 1, &entry, NULL) != MH_ERROR_NONE &&
		memcmp(&entry, &saved_entry, sizeof(entry)) == 0);

	for (i = 0; i < 77; i++) {
		CHECK(mh_atlas_remap_decode(remap_bytes, i, &remap, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&remap, &saved, sizeof(remap)) == 0);
	}

	CHECK(mh_atlas_remap_decode(remap_bytes, 78, &remap, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < 16; i++) {
		prepare_remap();

		if (i == 0)
			write_word(0, 65537);

		if (i == 1)
			write_word(4, 0);

		if (i == 2)
			write_word(8, 3);

		if (i == 3)
			write_word(12, 0);

		if (i == 4)
			write_word(16, 16385);

		if (i == 5)
			write_word(20, 0x7fc00000UL);

		if (i == 6)
			write_word(24, 0);

		if (i == 7)
			memset(remap_bytes + 28, 'x', 13);

		if (i == 8)
			remap_bytes[28] = '/';

		if (i == 9)
			write_word(41, 2);

		if (i == 10)
			write_word(49, MH_U32_MAX);

		if (i == 11)
			write_word(53, 1);

		/* Rectangle fits only after transposing. */
		if (i == 12)
			write_word(57, 0);

		if (i == 13)
			write_word(57, 2);

		if (i == 14)
			write_word(61, 0);

		if (i == 15)
			write_word(69, MH_U32_MAX);

		CHECK(mh_atlas_remap_decode(remap_bytes, 77, &remap, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&remap, &saved, sizeof(remap)) == 0);
	}

	memset(remap_bytes, 0, sizeof(remap_bytes));
	CHECK(mh_atlas_map_find(&remap, "IMAGE.IFF", &entry, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&entry, &saved_entry, sizeof(entry)) == 0);
	mh_atlas_map_free(&remap);

	return 0;
}

static int
check_groups(void)
{
	struct mh_atlas_map remap;
	struct mh_texture_groups groups, saved;
	unsigned int index, i;

	prepare_remap();
	CHECK(mh_atlas_remap_decode(remap_bytes, 77, &remap, NULL) ==
		MH_ERROR_NONE);
	memset(&groups, 0, sizeof(groups));
	index = 99;
	CHECK(mh_atlas_map_group(&remap, "IMAGE.IFF", &groups, &index, NULL) ==
		MH_ERROR_NONE);
	CHECK(groups.count == 1 && groups.pages[0] == 1 && index == 0);
	CHECK(mh_atlas_map_group(&remap, "image.iff", &groups, &index, NULL) ==
			MH_ERROR_NONE &&
		groups.count == 1);
	saved = groups;
	index = 99;
	CHECK(mh_atlas_map_group(&remap, "image.if", &groups, &index, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_atlas_map_group(&remap, "image.iffx", &groups, &index, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&groups, &saved, sizeof(groups)) == 0 && index == 99);

	for (i = 0; i < MH_TEXTURE_GROUPS; i++)
		groups.pages[i] = i + 2;

	groups.count = MH_TEXTURE_GROUPS;
	saved = groups;
	CHECK(mh_atlas_map_group(&remap, "image.iff", &groups, &index, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&groups, &saved, sizeof(groups)) == 0 && index == 99);
	groups.pages[55] = 1;
	CHECK(mh_atlas_map_group(&remap, "image.iff", &groups, &index, NULL) ==
			MH_ERROR_NONE &&
		index == 55);
	CHECK(groups.count == MH_TEXTURE_GROUPS);
	groups.count = MH_TEXTURE_GROUPS + 1;
	saved = groups;
	index = 99;
	CHECK(mh_atlas_map_group(&remap, "image.iff", &groups, &index, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&groups, &saved, sizeof(groups)) == 0 && index == 99);

	mh_atlas_map_free(&remap);

	return 0;
}

static int
check_authored_map(void)
{
	struct mh_atlas_map map;
	struct mh_atlas_map_entry entries[2], entry;
	struct mh_texture_groups groups;
	unsigned int index;

	memset(&map, 0, sizeof(map));
	memset(entries, 0, sizeof(entries));
	memset(&groups, 0, sizeof(groups));
	map.entries = entries;
	map.count = map.pages = 2;
	map.width = map.height = 8;
	map.scale[0] = map.scale[1] = 1;
	strcpy(entries[0].name, "paint.iff");
	entries[0].page = 1;
	entries[0].source_width = entries[0].scaled_width = 4;
	entries[0].source_height = entries[0].scaled_height = 8;
	entries[1] = entries[0];
	strcpy(entries[1].name, "lamp.iff");
	entries[1].page = 0;
	entries[1].transpose = 1;
	CHECK(mh_atlas_map_group(&map, "LAMP.IFF", &groups, &index, NULL) ==
		MH_ERROR_NONE);
	CHECK(index == 0 && groups.pages[0] == 0);
	CHECK(mh_atlas_map_group(&map, "paint.iff", &groups, &index, NULL) ==
		MH_ERROR_NONE);
	CHECK(index == 1 && groups.pages[1] == 1);
	CHECK(mh_atlas_map_find(&map, "lamp.iff", &entry, NULL) ==
		MH_ERROR_NONE);
	CHECK(entry.transpose == 1 && entry.scaled_width == 4);
	entries[1].page = 2;
	CHECK(mh_atlas_map_entry(&map, 1, &entry, NULL) == MH_ERROR_INVALID);
	CHECK(entry.page == 0);

	return 0;
}

int
main(void)
{
	unsigned char rgb[768];
	unsigned long colors[256], saved[256];
	unsigned int i;

	CHECK(check_remaps() == 0);
	CHECK(check_groups() == 0);
	CHECK(check_authored_map() == 0);

	for (i = 0; i < 256; i++) {
		rgb[i * 3] = (unsigned char)i;
		rgb[i * 3 + 1] = (unsigned char)(255 - i);
		rgb[i * 3 + 2] = 127;
	}

	CHECK(mh_palette_act_decode(rgb, sizeof(rgb), colors, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 256; i++)
		CHECK(colors[i] ==
			((unsigned long)i << 16) + ((255UL - i) << 8) + 127);

	memcpy(saved, colors, sizeof(saved));

	for (i = 0; i < 768; i++)
		CHECK(mh_palette_act_decode(rgb, i, colors, NULL) !=
			MH_ERROR_NONE);

	CHECK(mh_palette_act_decode(rgb, 769, colors, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved, colors, sizeof(saved)) == 0);
	puts("Resource metadata: atlas bounds, transpose, retained bytes, palette order and atomic failures passed");

	return 0;
}
