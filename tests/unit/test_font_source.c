#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "engine/render/scene.h"
#include "interfaces/font/source_backend.h"
#include "interfaces/storage/resource.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "font source:%d: %s\n", __LINE__, #e); \
			return 1;                                              \
		}                                                              \
	} while (0)

static enum mh_error_code
load_glyphs(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_font *output,
	struct mh_error *error_out)
{
	struct mh_font font;
	struct mh_file file;
	enum mh_error_code status;
	unsigned int i;

	(void)context;

	if (strcmp(root, "converted") != 0 ||
		(strcmp(reference, "Data/FONT1.FNT") != 0 &&
			strcmp(reference, "Data/font1.fnt") != 0))
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "unknown logical font");

	/* Different physical path and encoding from the requested font. */
	status = mh_resource_source_read(
		resources, "converted/art/letters.bin", &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (file.size != 1) {
		free(file.data);
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid test font");
	}

	memset(&font, 0, sizeof(font));
	font.image.width = 16;
	font.image.height = 32;
	font.cell_width = 1;
	font.height = 2;
	font.image.rgba = calloc(MH_FONT_GLYPHS * 2, 4);

	if (font.image.rgba == NULL) {
		free(file.data);
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test glyph allocation");
	}

	memset(font.image.rgba +
			(file.data[0] / 16 * 32 + file.data[0] % 16) * 4,
		255, 4);

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		font.advance[i] = 1;

	free(file.data);
	*output = font;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
check_driver_names(const struct mh_font_source_provider *fonts,
	const struct mh_resource_source *resources)
{
	struct mh_race_scene scene;
	struct mh_field field;
	struct mh_profile roster[MH_NAME_LABELS_MAX];
	const struct mh_indexed_image *image;

	memset(&scene, 0, sizeof(scene));
	memset(&field, 0, sizeof(field));
	memset(roster, 0, sizeof(roster));
	scene.assets.profile = MH_RENDER_SOFTWARE;
	field.count = 1;
	field.active_mask = 1;
	strcpy(roster[0].nick, "AB");
	roster[0].colors[0] = 0x123456UL;
	CHECK(mh_race_scene_names_prepare_from(fonts, resources, "converted",
		&scene, &field, roster, 1, NULL) == MH_ERROR_NONE);
	image = &scene.assets.software.names.images[0];
	CHECK(image->width == 4 && image->height == 3);
	CHECK(image->palette[3] == 0x12 && image->palette[4] == 0x34 &&
		image->palette[5] == 0x56);
	CHECK(image->indices[5] == 1 && image->indices[6] == 0);
	CHECK(image->indices[9] == 0 && image->indices[10] == 0);
	mh_name_images_free(&scene.assets.software.names);

	return 0;
}

int
main(void)
{
	static const struct mh_font_source_ops ops = {load_glyphs, NULL};
	static const struct mh_font_source_provider provider = {&ops, NULL};
	static const unsigned char letter = 'A';
	struct mh_resource_entry entry;
	const struct mh_resource_source *resources;
	struct mh_font font, saved;
	struct mh_font_program program, before;

	entry.path = "converted/art/letters.bin";
	entry.data = &letter;
	entry.size = 1;
	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_font_load(&provider, resources, "converted", "Data/FONT1.FNT",
		&font, NULL) == MH_ERROR_NONE);
	CHECK(font.cell_width == 1 && font.height == 2 &&
		font.advance['A'] == 1 && font.image.rgba[129 * 4 + 3] == 255);
	CHECK(font.image.rgba[130 * 4 + 3] == 0);
	saved = font;
	CHECK(mh_font_load(&provider, resources, "converted", "missing", &font,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(&font, &saved, sizeof(font)) == 0);
	memset(&program, 0, sizeof(program));
	program.height = 7;
	before = program;
	CHECK(mh_font_load_program(&provider, resources, "converted",
		"Data/FONT1.FNT", &program,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&program, &before, sizeof(program)) == 0);
	CHECK(check_driver_names(&provider, resources) == 0);
	mh_memory_resources_close(resources);
	CHECK(font.image.rgba[129 * 4] == 255);
	free(font.image.rgba);
	puts("Selected fonts preserve glyph metrics and ownership without FNT bytes");

	return 0;
}
