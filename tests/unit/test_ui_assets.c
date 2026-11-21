#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/font.h"
#include "engine/assets/tga.h"
#include "engine/core/error.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
write_word(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

static void
check_tga_16(void)
{
	static const unsigned char pixels[] = {
		0x00, 0xfc, 0xe0, 0x03, 0x1f, 0x80, 0x5f, 0xbc};

	static const unsigned char colors[] = {
		255, 0, 0, 0, 255, 0, 0, 0, 255, 123, 16, 255};

	unsigned char data[26];
	struct mh_image image, saved;
	unsigned int origin, alpha, i, x, y, destination;
	enum mh_error_code status;

	memset(data, 0, sizeof(data));
	data[2] = 2;
	data[12] = data[14] = 2;
	data[16] = 16;
	memcpy(data + 18, pixels, sizeof(pixels));

	for (origin = 0; origin < 4; origin++) {
		for (alpha = 0; alpha < 2; alpha++) {
			data[17] = (unsigned char)(origin * 16 + alpha);
			status = mh_image_tga_decode(
				data, sizeof(data), 0, &image, NULL);
			CHECK(status == MH_ERROR_NONE);

			if (status != MH_ERROR_NONE)
				continue;

			for (i = 0; i < 4; i++) {
				x = (origin & 1) ? 1 - i % 2 : i % 2;
				y = (origin & 2) ? i / 2 : 1 - i / 2;
				destination = (y * 2 + x) * 4;
				CHECK(memcmp(image.rgba + destination,
					colors + i * 3, 3) == 0);
				CHECK(image.rgba[destination + 3] ==
					(alpha && i == 1 ? 0 : 255));
			}

			free(image.rgba);
		}
	}

	memset(&saved, 0, sizeof(saved));
	saved.width = 123;
	image = saved;

	for (i = 0; i < sizeof(data); i++) {
		CHECK(mh_image_tga_decode(data, i, 0, &image, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
	}

	data[17] = 8;
	CHECK(mh_image_tga_decode(data, sizeof(data), 0, &image, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
	data[17] = 32;
	data[18] = 0x1f;
	data[19] = 0x7c;
	status = mh_image_tga_decode(data, sizeof(data), 1, &image, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(image.rgba[0] == 255 && image.rgba[1] == 0 &&
			image.rgba[2] == 255 && image.rgba[3] == 0);
		free(image.rgba);
	}
}

static void
check_tga(void)
{
	unsigned char data[64];
	struct mh_image image, saved;
	unsigned int i, origin;
	enum mh_error_code status;
	static const unsigned char pixels[12] = {
		0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 0, 255};

	memset(&saved, 0, sizeof(saved));
	saved.width = 123;
	memset(data, 0, sizeof(data));
	data[2] = 2;
	data[12] = data[14] = 2;
	data[16] = 24;
	memcpy(data + 18, pixels, sizeof(pixels));

	for (origin = 0; origin < 4; origin++) {
		data[17] = (unsigned char)(origin * 16);
		status = mh_image_tga_decode(data, 30, 1, &image, NULL);
		CHECK(status == MH_ERROR_NONE);

		if (status == MH_ERROR_NONE) {
			CHECK(image.width == 2 && image.height == 2);

			for (i = 0; i < 4; i++) {
				unsigned int x, y, dest;

				x = (origin & 1) ? 1 - i % 2 : i % 2;
				y = (origin & 2) ? i / 2 : 1 - i / 2;
				dest = (y * 2 + x) * 4;
				CHECK(image.rgba[dest] == pixels[i * 3 + 2]);
				CHECK(image.rgba[dest + 1] ==
					pixels[i * 3 + 1]);
				CHECK(image.rgba[dest + 2] == pixels[i * 3]);
				CHECK(image.rgba[dest + 3] ==
					(i == 3 ? 0 : 255));
			}

			free(image.rgba);
		}
	}

	image = saved;

	for (i = 0; i < 30; i++) {
		CHECK(mh_image_tga_decode(data, i, 1, &image, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
	}

	data[2] = 10;
	CHECK(mh_image_tga_decode(data, sizeof(data), 0, &image, NULL) !=
		MH_ERROR_NONE);
	data[2] = 2;
	data[17] = 192;
	CHECK(mh_image_tga_decode(data, sizeof(data), 0, &image, NULL) !=
		MH_ERROR_NONE);
	data[17] = 0;
	data[12] = 0;
	CHECK(mh_image_tga_decode(data, sizeof(data), 0, &image, NULL) !=
		MH_ERROR_NONE);
	data[12] = data[13] = data[14] = data[15] = 255;
	CHECK(mh_image_tga_decode(data, sizeof(data), 0, &image, NULL) !=
		MH_ERROR_NONE);

	/*
	 * Nonzero palette origin, ID bytes, indexed descriptor used by
	 * Results.TGA.
	 */
	memset(data, 0, sizeof(data));
	data[0] = 2;
	data[1] = data[2] = 1;
	data[3] = 9;
	data[5] = 2;
	data[7] = 24;
	data[12] = 2;
	data[14] = 1;
	data[16] = 8;
	data[17] = 0x28;
	memcpy(data + 20, pixels, 6);
	data[26] = 10;
	data[27] = 9;
	status = mh_image_tga_decode(data, 28, 0, &image, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(image.rgba[1] == 255 && image.rgba[4] == 255);
		free(image.rgba);
	}

	data[27] = 8;
	CHECK(mh_image_tga_decode(data, 28, 0, &image, NULL) != MH_ERROR_NONE);
	data[27] = 11;
	CHECK(mh_image_tga_decode(data, 28, 0, &image, NULL) != MH_ERROR_NONE);
	data[27] = 9;
	CHECK(mh_image_tga_decode(data, 27, 0, &image, NULL) != MH_ERROR_NONE);
	memset(data, 0, sizeof(data));
	data[2] = 3;
	data[12] = 2;
	data[14] = 1;
	data[16] = 8;
	data[18] = 37;
	data[19] = 219;
	status = mh_image_tga_decode(data, 20, 0, &image, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(image.rgba[0] == 37 && image.rgba[6] == 219);
		free(image.rgba);
	}

	data[2] = 2;
	data[12] = 1;
	data[16] = 32;
	data[17] = 8;
	data[18] = 3;
	data[19] = 2;
	data[20] = 1;
	data[21] = 17;
	status = mh_image_tga_decode(data, 22, 0, &image, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(image.rgba[0] == 1 && image.rgba[3] == 17);
		free(image.rgba);
	}
}

static void
check_tga_indexed(void)
{
	static const unsigned char expected[4][4] = {
		{9, 11, 9, 10}, {11, 9, 10, 9}, {9, 10, 9, 11}, {10, 9, 11, 9}};

	unsigned char data[36];
	struct mh_indexed_image image, saved;
	unsigned int origin, depth, i, start, size;
	enum mh_error_code status;

	memset(&saved, 0, sizeof(saved));
	saved.width = 123;

	for (depth = 3; depth <= 4; depth++) {
		memset(data, 0, sizeof(data));
		data[0] = 2;
		data[1] = data[2] = 1;
		data[3] = 9;
		data[5] = 3;
		data[7] = (unsigned char)(depth * 8);
		data[12] = data[14] = 2;
		data[16] = 8;

		/* Two identical RGB entries must retain distinct indices. */
		data[22] = data[22 + depth] = 43;
		data[20 + depth * 2] = 71;
		start = 20 + depth * 3;
		memcpy(data + start, expected[2], 4);
		size = start + 4;

		for (origin = 0; origin < 4; origin++) {
			data[17] = (unsigned char)(origin * 16);
			status = mh_image_tga_decode_indexed(
				data, size, &image, NULL);
			CHECK(status == MH_ERROR_NONE);

			if (status != MH_ERROR_NONE)
				continue;

			CHECK(image.width == 2 && image.height == 2 &&
				image.size == 4);
			CHECK(image.colors == 12);
			CHECK(memcmp(image.indices, expected[origin], 4) == 0);
			CHECK(image.palette[27] == 43 &&
				image.palette[30] == 43);
			CHECK(image.palette[35] == 71);
			CHECK(image.palette[0] == 0 && image.palette[767] == 0);
			free(image.indices);
		}

		image = saved;

		for (i = 0; i < size; i++) {
			CHECK(mh_image_tga_decode_indexed(
				data, i, &image, NULL) != MH_ERROR_NONE);
			CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
		}

		data[start] = 8;
		CHECK(mh_image_tga_decode_indexed(data, size, &image, NULL) ==
			MH_ERROR_RANGE);
		CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
		data[start] = 12;
		CHECK(mh_image_tga_decode_indexed(data, size, &image, NULL) ==
			MH_ERROR_RANGE);
		CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
	}

	data[1] = 0;
	data[2] = 3;
	CHECK(mh_image_tga_decode_indexed(data, sizeof(data), &image, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
}

static void
prepare_font(unsigned char *data)
{
	unsigned int i;
	static const unsigned char commands[] = {1, 248, 136, 88, 0, 102, 137,
		88, 1, 137, 88, 3, 221, 80, 7, 1, 240, 136, 88, 14, 233, 255,
		254, 253, 252};

	memset(data, 0, 2082);
	write_word(data, 4);

	for (i = 0; i < 256; i++) {
		write_word(data + 4 + i * 8, 2052);
		write_word(data + 8 + i * 8, 15);
	}

	data[2052] = 233;
	write_word(data + 4 + 65 * 8, 2057);
	memcpy(data + 2057, commands, sizeof(commands));
}

static void
check_font(void)
{
	unsigned char data[2082];
	struct mh_font font, saved;
	struct mh_font_program program, saved_program;
	enum mh_error_code status;
	size_t pixel;
	unsigned int i;

	prepare_font(data);
	CHECK(mh_font_fnt_decode_program(data, sizeof(data), &program, NULL) ==
		MH_ERROR_NONE);
	CHECK(program.width == 240 && program.rows == 64 &&
		program.cell_width == 15 && program.height == 4);
	pixel = (size_t)(4 * 4 + 1) * 240 + 15;
	CHECK(program.bytes[pixel] == MH_FONT_LOW);
	CHECK(program.bytes[pixel + 1] == MH_FONT_LOW);
	CHECK(program.bytes[pixel + 2] == MH_FONT_HIGH);
	CHECK(program.bytes[pixel + 13] == MH_FONT_WIDE_LOW);
	CHECK(program.bytes[pixel + 14] == MH_FONT_HIGH);
	CHECK(program.bytes[pixel - 240] == MH_FONT_SKIP);
	CHECK(program.bytes[pixel + 480 + 14] == MH_FONT_LOW);
	free(program.bytes);

	status = mh_font_fnt_decode(data, sizeof(data), &font, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(font.height == 4 && font.advance[65] == 15 &&
			font.image.width == 240);
		pixel = ((size_t)(4 * 4 + 1) * 240 + 15) * 4;

		for (i = 0; i < 15; i++)
			CHECK(font.image.rgba[pixel + i * 4 + 3] == 255);

		CHECK(font.image.rgba[pixel - 240 * 4 + 3] == 0);
		CHECK(font.image.rgba[pixel + 240 * 4 + 3] == 0);
		CHECK(font.image.rgba[pixel + 2 * 240 * 4 + 14 * 4 + 3] == 255);
		free(font.image.rgba);
	}

	memset(&saved, 0, sizeof(saved));
	saved.height = 123;
	font = saved;
	memset(&saved_program, 0, sizeof(saved_program));
	saved_program.height = 123;
	program = saved_program;

	for (i = 0; i < sizeof(data); i++) {
		CHECK(mh_font_fnt_decode(data, i, &font, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&font, &saved, sizeof(font)) == 0);
		CHECK(mh_font_fnt_decode_program(data, i, &program, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&program, &saved_program, sizeof(program)) == 0);
	}

	data[2057] = 0xc3;
	CHECK(mh_font_fnt_decode(data, sizeof(data), &font, NULL) !=
		MH_ERROR_NONE);
	prepare_font(data);
	data[2061] = 15;
	CHECK(mh_font_fnt_decode(data, sizeof(data), &font, NULL) !=
		MH_ERROR_NONE);
	prepare_font(data);
	data[2074] = 240;
	CHECK(mh_font_fnt_decode(data, sizeof(data), &font, NULL) !=
		MH_ERROR_NONE);
	prepare_font(data);
	write_word(data + 4, 2051);
	CHECK(mh_font_fnt_decode(data, sizeof(data), &font, NULL) !=
		MH_ERROR_NONE);
	prepare_font(data);
	write_word(data + 8, 65);
	CHECK(mh_font_fnt_decode(data, sizeof(data), &font, NULL) !=
		MH_ERROR_NONE);
	prepare_font(data);
	write_word(data, 0);
	CHECK(mh_font_fnt_decode(data, sizeof(data), &font, NULL) !=
		MH_ERROR_NONE);

	/* A missing terminator must not consume the following glyph program. */
	prepare_font(data);
	data[2052] = 1;
	data[2053] = 248;
	CHECK(mh_font_fnt_decode(data, sizeof(data), &font, NULL) !=
		MH_ERROR_NONE);
}

int
main(void)
{
	check_tga();
	check_tga_16();
	check_tga_indexed();
	check_font();

	return failures != 0;
}
