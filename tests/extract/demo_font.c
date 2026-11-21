#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/demo_font.h"
#include "engine/assets/font.h"
#include "engine/core/binary.h"
#include "engine/core/file.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "demo font line %d: %s\n", __LINE__,   \
				#expression);                                  \
			return 1;                                              \
		}                                                              \
	} while (0)

enum {
	GLYPH_TABLE_BYTES = 4 + MH_FONT_GLYPHS * 8,
	GLYPH_BYTES = 15
};

static int
check_conversion(void)
{
	static const unsigned char commands[GLYPH_BYTES] = {0x66, 0x89, 0x58, 0,
		1, 0xf8, 0x66, 0x89, 0x58, 2, 0xe9, 0, 0, 0, 0};

	unsigned char bytes[GLYPH_TABLE_BYTES + GLYPH_BYTES];
	struct mh_file source, prepared, saved;
	struct mh_font font;
	unsigned int i;

	memset(bytes, 0, sizeof(bytes));
	mh_write_u32le(bytes, 2);

	for (i = 0; i < MH_FONT_GLYPHS; i++) {
		mh_write_u32le(bytes + 4 + i * 8, GLYPH_TABLE_BYTES);
		mh_write_u32le(bytes + 8 + i * 8, 4);
	}

	memcpy(bytes + GLYPH_TABLE_BYTES, commands, sizeof(commands));
	source.data = bytes;
	source.size = sizeof(bytes);
	CHECK(mh_font_fnt_byte_pixels(&source, &prepared, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_font_fnt_decode(prepared.data, prepared.size, &font, NULL) ==
		MH_ERROR_NONE);
	CHECK(font.height == 2 && font.cell_width == 2);
	CHECK(font.image.rgba[3] == 255 && font.image.rgba[7] == 0);
	CHECK(font.image.rgba[font.image.width * 4 + 3] == 0);
	CHECK(font.image.rgba[font.image.width * 4 + 7] == 255);

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		CHECK(font.advance[i] == 2);

	free(font.image.rgba);
	free(prepared.data);
	memset(&saved, 0x5a, sizeof(saved));
	prepared = saved;
	mh_write_u32le(bytes + 8, 5);
	CHECK(mh_font_fnt_byte_pixels(&source, &prepared, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&prepared, &saved, sizeof(saved)) == 0);
	mh_write_u32le(bytes + 8, 4);

	/* One covered byte in a two-byte pixel must not be silently rounded. */
	bytes[GLYPH_TABLE_BYTES] = 0x88;
	bytes[GLYPH_TABLE_BYTES + 1] = 0x58;
	bytes[GLYPH_TABLE_BYTES + 2] = 0;
	bytes[GLYPH_TABLE_BYTES + 3] = 0xe9;
	CHECK(mh_font_fnt_byte_pixels(&source, &prepared, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&prepared, &saved, sizeof(saved)) == 0);

	return 0;
}

int
main(int argc, char *argv[])
{
	struct mh_file source, prepared;
	struct mh_error error;
	enum mh_error_code status;
	int success;

	if (argc == 1)
		return check_conversion();

	if (argc != 2) {
		fprintf(stderr, "Usage: test-demo-font [WORD-FONT.FNT]\n");
		return 1;
	}

	status = mh_file_read(argv[1], &source, &error);

	if (status == MH_ERROR_NONE) {
		status = mh_font_fnt_byte_pixels(&source, &prepared, &error);
		free(source.data);
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	success = fwrite(prepared.data, 1, prepared.size, stdout) ==
			prepared.size &&
		fflush(stdout) == 0;
	free(prepared.data);

	return success ? 0 : 1;
}
