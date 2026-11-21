#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/sprite/spr_provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/assets/spr.h"
#include "engine/core/error.h"
#include "interfaces/sprite/collection.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "SPR line %d: %s\n", __LINE__, #x);    \
			return 1;                                              \
		}                                                              \
	} while (0)
static unsigned char data[2048], saved_data[2048];

static void
write_u32(unsigned int offset, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		data[offset + i] = (unsigned char)(value >> (8 * i));
}

static size_t
indexed_fixture(void)
{
	static const unsigned char program[] = {0xc6, 0x40, 0,
		0, /* Index zero is opaque. */
		0x66, 0xc7, 0x40, 2, 1, 2, 5, 128, 0, 0, 0, 0xc7, 0x40, 0, 3, 4,
		5, 6, 5, 128, 255, 255, 255, 0xc3};

	unsigned int i;

	memset(data, 0, sizeof(data));
	write_u32(0, 0x80000001UL);
	memcpy(data + 4, "eightchr", 8);
	write_u32(12, 784);

	for (i = 0; i < 256; i++) {
		data[16 + i * 3] = (unsigned char)i;
		data[17 + i * 3] = (unsigned char)(255 - i);
		data[18 + i * 3] = 63;
	}

	write_u32(784, 2);
	write_u32(788, 132);
	write_u32(792, 2);
	write_u32(796, 20);
	write_u32(800, 804 - 784 + 8 + sizeof(program) + 1);
	write_u32(804, 8);
	write_u32(808, 8 + sizeof(program));
	memcpy(data + 812, program, sizeof(program));
	data[812 + sizeof(program)] = 0xc3;
	i = 813 + sizeof(program);
	write_u32(i, 8);
	write_u32(i + 4, 9);
	data[i + 8] = data[i + 9] = 0xc3;

	return i + 10;
}

static int
rejected(size_t size)
{
	struct mh_spr archive, saved;

	memset(&archive, 0xa5, sizeof(archive));
	saved = archive;
	CHECK(mh_sprite_spr_read(data, size, &archive, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&archive, &saved, sizeof(archive)) == 0);

	return 0;
}

static size_t
literal_fixture(int columns)
{
	static const unsigned char row[] = {0xc7, 0x40, 0, 0, 0, 0, 0xfc, 0xc3};
	static const unsigned char column[] = {
		0x50, 0x66, 0xc7, 0, 0, 0, /* Opaque black. */
		1, 0xf8, 0x66, 0xc7, 0, 255, 255, 1, 0xf8, 0x66, 0xc7, 0, 0,
		0xf8, 1, 0xf8, 0x58,
		0xc3 /* One-past-height origin is restored, never written. */
	};
	size_t length;

	memset(data, 0, sizeof(data));
	write_u32(0, 1);
	memcpy(data + 4, "meter", 5);
	write_u32(12, 784);
	write_u32(784, 1);
	write_u32(788, columns ? 3 : 4);
	write_u32(792, 2);
	write_u32(796, 16);
	length = columns ? sizeof(column) : sizeof(row);
	write_u32(800, 8);
	write_u32(804, 8 + length);
	memcpy(data + 808, columns ? column : row, length);

	if (columns)
		data[808 + length++] = 0x50;

	if (columns)
		data[808 + length++] = 0x58;

	data[808 + length++] = 0xc3;

	return 808 + length;
}

static int
literal_colors(void)
{
	struct mh_spr archive;
	struct mh_sprite_words literal, saved_literal;
	struct mh_image image, saved_image;
	size_t size;
	unsigned int i;

	size = literal_fixture(0);
	CHECK(mh_sprite_spr_read(data, size, &archive, NULL) == MH_ERROR_NONE);
	CHECK(mh_sprite_spr_decode_literal(&archive, 0, 0, 0, &literal, NULL) ==
		MH_ERROR_NONE);
	CHECK(literal.width == 2 && literal.height == 2);
	CHECK(memcmp(literal.pixels, "\0\0\377\0\374\377\0\0\0\0\0\0", 12) ==
		0);
	free(literal.pixels);
	CHECK(mh_sprite_spr_decode_literal_rgba(
		&archive, 0, 0, 0, 5, &image, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(image.rgba, "\0\0\0\377\377\0\0\377\0\0\0\0\0\0\0\0",
		16) == 0);
	free(image.rgba);
	CHECK(mh_sprite_spr_decode_literal_rgba(
		&archive, 0, 0, 0, 6, &image, NULL) == MH_ERROR_NONE);
	CHECK(image.rgba[4] == 255 && image.rgba[5] == 130 &&
		image.rgba[6] == 0);
	free(image.rgba);
	mh_sprite_spr_free(&archive);
	size = literal_fixture(1);
	CHECK(mh_sprite_spr_read(data, size, &archive, NULL) == MH_ERROR_NONE);
	CHECK(mh_sprite_spr_decode_literal(&archive, 0, 0, 1, &literal, NULL) ==
		MH_ERROR_NONE);
	CHECK(literal.width == 2 && literal.height == 3);
	CHECK(memcmp(literal.pixels,
		"\0\0\377\0\0\0\377\377\377\0\0\0\0\370\377\0\0\0",
		18) == 0);
	free(literal.pixels);
	CHECK(mh_sprite_spr_decode_literal_rgba(
		&archive, 0, 0, 1, 6, &image, NULL) == MH_ERROR_NONE);
	CHECK(image.rgba[3] == 255 && image.rgba[7] == 0);
	CHECK(memcmp(image.rgba + 8, "\377\377\377\377", 4) == 0);
	CHECK(memcmp(image.rgba + 16, "\377\0\0\377", 4) == 0);
	free(image.rgba);
	mh_sprite_spr_free(&archive);

	/* Truncate every position in the column program, including POP/RET. */
	for (i = 1; i < 24; i++) {
		size = literal_fixture(1);
		write_u32(804, 8 + i);
		CHECK(mh_sprite_spr_read(data, size, &archive, NULL) ==
			MH_ERROR_NONE);
		memset(&literal, 0xa5, sizeof(literal));
		saved_literal = literal;
		CHECK(mh_sprite_spr_decode_literal(&archive, 0, 0, 1, &literal,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&literal, &saved_literal, sizeof(literal)) == 0);
		mh_sprite_spr_free(&archive);
	}

	for (i = 0; i < 11; i++) {
		size = literal_fixture(1);

		if (i == 0)
			data[808] = 0xc3; /* Missing save. */

		if (i == 1)
			data[815] = 0xf0; /* ADD wrong register. */

		if (i == 2)
			data[810] = 0xe8; /* No CALL. */

		if (i == 3)
			data[811] = 0x40; /* No offset stores in columns. */

		if (i == 4)
			data[830] = 0x50; /* No nested PUSH. */

		if (i == 5)
			data[831] = 0x58; /* Missing RET. */

		if (i == 6)
			write_u32(788, 2); /* Final step crosses height. */

		if (i == 7)
			write_u32(788, 1); /* Store crosses height. */

		if (i == 8) {
			size = literal_fixture(0);
			write_u32(788, 3);
		}

		if (i == 9) {
			size = literal_fixture(0);
			data[808] = 0xc6;
			data[812] = 0xc3;
		}

		CHECK(mh_sprite_spr_read(data, size, &archive, NULL) ==
			MH_ERROR_NONE);
		memset(&image, 0xa5, sizeof(image));
		saved_image = image;
		CHECK(mh_sprite_spr_decode_literal_rgba(&archive, 0, 0, i < 8,
			i == 10 ? 4 : 5, &image, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&image, &saved_image, sizeof(image)) == 0);
		CHECK(mh_sprite_spr_decode_literal(&archive, 1, 0, 1, &literal,
			NULL) != MH_ERROR_NONE);
		CHECK(mh_sprite_spr_decode_literal(&archive, 0, 1, 1, &literal,
			NULL) != MH_ERROR_NONE);
		CHECK(mh_sprite_spr_decode_literal(&archive, 0, 0, 2, &literal,
			NULL) != MH_ERROR_NONE);
		mh_sprite_spr_free(&archive);
	}

	return 0;
}

static int
check_collection(unsigned int mode)
{
	struct mh_resource_entry resource;
	const struct mh_resource_source *resources;
	struct mh_sprite_collection *collection;
	struct mh_sprite_info info;
	struct mh_sprite_indexed indexed;
	struct mh_image image;
	struct mh_image_palette palette;
	const char *name;
	size_t size;

	size = mode == 0 ? indexed_fixture() : literal_fixture(mode == 2);
	name = mode == 0 ? "eightchr" : mode == 1 ? "snum0" : "meter";

	if (mode == 1) {
		memset(data + 4, 0, 8);
		memcpy(data + 4, name, 5);
	}

	resource.path = "source/art.spr";
	resource.data = data;
	resource.size = size;
	CHECK(mh_memory_resources_open(&resource, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_sprite_collection_open(mh_sprite_spr_source(), resources,
		"source", "art.spr",
		mode == 0 ? MH_PICTURE_INDEXED : MH_PICTURE_RGB565,
		&collection, NULL) == MH_ERROR_NONE);

	/* The original provider owns its encoded bytes, too. */
	memset(data, 0, sizeof(data));
	CHECK(mh_sprite_collection_info(collection, name, &info, NULL) ==
		MH_ERROR_NONE);
	CHECK(info.frames == (mode == 0 ? 2U : 1U));
	CHECK(info.width == (mode == 0 ? 132U : 2U));
	CHECK(info.height == (mode == 2 ? 3U : 2U));
	CHECK(mh_sprite_collection_info(collection, "absent", &info, NULL) ==
		MH_ERROR_NOT_FOUND);

	if (mode == 0) {
		CHECK(mh_sprite_collection_palette(
			collection, &palette, NULL) == MH_ERROR_NONE);
		CHECK(palette.count == 256 && palette.rgb[0] == 0 &&
			palette.rgb[1] == 255 && palette.rgb[2] == 63);
		CHECK(mh_sprite_collection_indexed(collection, name, 0,
			&indexed, NULL) == MH_ERROR_NONE);
		CHECK(indexed.pixels[0] == 0 && indexed.pixels[1] == 255 &&
			indexed.pixels[3] == 0);
	} else {
		CHECK(mh_sprite_collection_indexed(collection, name, 0,
			&indexed, NULL) == MH_ERROR_UNSUPPORTED);
		CHECK(mh_sprite_collection_palette(collection, &palette,
			NULL) == MH_ERROR_UNSUPPORTED);
	}

	CHECK(mh_sprite_collection_rgba(collection, name, 0, &image, NULL) ==
		MH_ERROR_NONE);
	mh_sprite_collection_close(collection);
	mh_memory_resources_close(resources);
	CHECK(image.rgba[3] == 255);

	if (mode == 0) {
		CHECK(image.rgba[0] == 0 && image.rgba[1] == 255 &&
			image.rgba[2] == 63 && image.rgba[7] == 0);
		CHECK(indexed.pixels[1] == 255);
		free(indexed.pixels);
	} else if (mode == 1)
		CHECK(image.rgba[4] == 255 && image.rgba[5] == 130 &&
			image.rgba[6] == 0);
	else
		CHECK(image.rgba[7] == 0 && image.rgba[8] == 255 &&
			image.rgba[16] == 255);

	free(image.rgba);

	return 0;
}

int
main(void)
{
	struct mh_spr archive;
	struct mh_image image, saved;
	struct mh_sprite_indexed indexed;
	size_t size, i;
	unsigned int index;

	CHECK(literal_colors() == 0);
	CHECK(check_collection(0) == 0);
	CHECK(check_collection(1) == 0);
	CHECK(check_collection(2) == 0);
	size = indexed_fixture();
	memcpy(saved_data, data, sizeof(data));
	CHECK(mh_sprite_spr_read(data, size, &archive, NULL) == MH_ERROR_NONE &&
		archive.count == 1);
	CHECK(mh_sprite_spr_find(&archive, "eightchr") == &archive.entries[0]);
	CHECK(mh_sprite_spr_find(&archive, "eightch") == NULL &&
		mh_sprite_spr_find(&archive, "Eightchr") == NULL);
	CHECK(mh_sprite_spr_find(&archive, "") == NULL &&
		mh_sprite_spr_find(&archive, "eightchrs") == NULL);
	CHECK(mh_sprite_spr_decode_rgba(&archive, 0, 0, &image, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_sprite_spr_decode_indexed(&archive, 0, 0, &indexed, NULL) ==
		MH_ERROR_NONE);
	CHECK(image.width == 132 && image.height == 2);

	for (i = 0; i < 264; i++) {
		index = i == 0		      ? 0
			: i == 2	      ? 1
			: i == 3	      ? 2
			: i >= 128 && i < 132 ? (unsigned int)i - 125
					: 256;

		if (index == 256)
			CHECK(image.rgba[i * 4] == 0 &&
				image.rgba[i * 4 + 3] == 0);
		else
			CHECK(image.rgba[i * 4] == index &&
				image.rgba[i * 4 + 1] == 255 - index &&
				image.rgba[i * 4 + 2] == 63 &&
				image.rgba[i * 4 + 3] == 255);
		CHECK(indexed.pixels[i * 2] == (index == 256 ? 0 : index) &&
			indexed.pixels[i * 2 + 1] == image.rgba[i * 4 + 3]);
	}

	free(image.rgba);
	free(indexed.pixels);
	CHECK(mh_sprite_spr_decode_rgba(&archive, 0, 1, &image, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 264 * 4; i++)
		CHECK(image.rgba[i] == 0);

	free(image.rgba);
	mh_sprite_spr_free(&archive);
	CHECK(archive.entries == NULL && archive.data == NULL &&
		archive.count == 0);
	CHECK(memcmp(data, saved_data, sizeof(data)) == 0);

	/*
	 * Every structural truncation fails without publishing partial
	 * metadata.
	 */
	for (i = 0; i < size; i++)
		CHECK(rejected(i) == 0);

	write_u32(0, 4097);
	CHECK(rejected(size) == 0);
	memcpy(data, saved_data, sizeof(data));
	write_u32(12, 16);
	CHECK(rejected(size) == 0);
	memcpy(data, saved_data, sizeof(data));
	data[6] = 0;
	CHECK(rejected(size) == 0);
	memcpy(data, saved_data, sizeof(data));
	write_u32(784, 0);
	CHECK(rejected(size) == 0);
	memcpy(data, saved_data, sizeof(data));
	write_u32(788, 4097);
	CHECK(rejected(size) == 0);
	memcpy(data, saved_data, sizeof(data));
	write_u32(800, 20);
	CHECK(rejected(size) == 0);
	memcpy(data, saved_data, sizeof(data));
	write_u32(804, 4);
	CHECK(rejected(size) == 0);
	memcpy(data, saved_data, sizeof(data));
	write_u32(808, 8);
	CHECK(rejected(size) == 0);
	memcpy(data, saved_data, sizeof(data));

	/* Mutate before parsing, then exercise the bounded program decoder. */
	for (index = 0; index < 8; index++) {
		memcpy(data, saved_data, sizeof(data));

		if (index == 0)
			data[812] = 0xe8; /* CALL is never followed. */

		if (index == 1)
			data[813] = 0x48; /* Wrong ModR/M register. */

		if (index == 2)
			data[814] = 0x80; /* Negative displacement. */

		if (index == 3)
			data[834] = 0x66; /* Unsupported word command. */

		if (index == 4)
			write_u32(823, 133); /* Origin exceeds width. */

		if (index == 5)
			write_u32(835,
				0xffffff7fUL); /* Restore crosses row start. */

		if (index == 6)
			write_u32(808, 35); /* End the row just before RET. */

		if (index == 7)
			write_u32(835, 0); /* RET must restore the origin. */

		CHECK(mh_sprite_spr_read(data, size, &archive, NULL) ==
			MH_ERROR_NONE);
		memset(&image, 0xa5, sizeof(image));
		saved = image;
		CHECK(mh_sprite_spr_decode_rgba(&archive, 0, 0, &image, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
		CHECK(mh_sprite_spr_decode_rgba(&archive, 1, 0, &image, NULL) !=
				MH_ERROR_NONE &&
			mh_sprite_spr_decode_rgba(
				&archive, 0, 2, &image, NULL) != MH_ERROR_NONE);
		mh_sprite_spr_free(&archive);
	}

	for (index = 1; index < 28; index++) {
		memcpy(data, saved_data, sizeof(data));
		write_u32(808, 8 + index);
		CHECK(mh_sprite_spr_read(data, size, &archive, NULL) ==
			MH_ERROR_NONE);
		memset(&image, 0xa5, sizeof(image));
		saved = image;
		CHECK(mh_sprite_spr_decode_rgba(&archive, 0, 0, &image, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
		mh_sprite_spr_free(&archive);
	}

	puts("SPR: names, frames, literal spans, transparency, truncations and bounded bytecode passed");

	return 0;
}
