#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/font.h"
#include "engine/assets/profile.h"
#include "engine/assets/tbl.h"
#include "engine/assets/tga.h"
#include "engine/core/error.h"

enum {
	RGBA_PIXEL_BYTES = 4
};

/* Consumes the input bytes, including on decode failure. */
static int
inspect_profile(struct mh_file *file)
{
	struct mh_profile profile;
	unsigned char *text;
	size_t size;
	struct mh_error error;
	enum mh_error_code status;

	status = mh_tbl_decode(file->data, file->size, &text, &size, &error);
	free(file->data);

	if (status == MH_ERROR_NONE) {
		status = mh_profile_adp_decode(text, size, &profile, &error);
		free(text);
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	printf("%s\t%s\t%s\n", profile.name, profile.nick, profile.picture);

	return ferror(stdout) ? 1 : 0;
}

static int
write_image(const char *path, const struct mh_image *image)
{
	FILE *output;
	size_t bytes;
	int success;

	output = fopen(path, "wb");

	if (output == NULL)
		return 0;

	bytes = (size_t)image->width * image->height * RGBA_PIXEL_BYTES;
	success = fwrite(image->rgba, 1, bytes, output) == bytes;

	if (fclose(output) != 0)
		success = 0;

	return success;
}

int
main(int argc, char *argv[])
{
	struct mh_file file;
	struct mh_image image;
	struct mh_font font;
	struct mh_error error;
	enum mh_error_code status;
	int success;

	if (argc < 3 || argc > 4 ||
		(strcmp(argv[1], "tga") != 0 &&
			strcmp(argv[1], "sprite") != 0 &&
			strcmp(argv[1], "font") != 0 &&
			strcmp(argv[1], "profile") != 0) ||
		(strcmp(argv[1], "profile") == 0 && argc != 3)) {
		fprintf(stderr,
			"Usage: mh-ui-info tga|sprite|font FILE [OUTPUT.rgba]\n       mh-ui-info profile FILE.adp\n");
		return 1;
	}

	status = mh_file_read(argv[2], &file, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	if (strcmp(argv[1], "profile") == 0) {
		return inspect_profile(&file);
	}

	if (strcmp(argv[1], "font") == 0) {
		status =
			mh_font_fnt_decode(file.data, file.size, &font, &error);

		if (status == MH_ERROR_NONE)
			image = font.image;
	} else
		status = mh_image_tga_decode(file.data, file.size,
			strcmp(argv[1], "sprite") == 0, &image, &error);

	free(file.data);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	printf("%u %u\n", image.width, image.height);
	success = !ferror(stdout);

	if (argc == 4 && !write_image(argv[3], &image))
		success = 0;

	free(image.rgba);

	if (!success)
		fprintf(stderr, "UI inspection output failed\n");

	return success ? 0 : 1;
}
