#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/ilbm.h"
#include "engine/assets/myo.h"
#include "engine/assets/myw.h"
#include "engine/assets/pdi.h"
#include "engine/core/error.h"
#include "engine/geometry/model.h"
#include "engine/geometry/world.h"

enum {
	RGBA_PIXEL_BYTES = 4
};

struct asset_counts {
	unsigned long	models, faces, archives, images, worlds;
};

static enum mh_error_code
check_world(const struct mh_file *file, struct asset_counts *counts,
	struct mh_error *error_out)
{
	struct mh_world world;
	size_t index;
	enum mh_error_code status;

	status = mh_world_myw_decode(file->data, file->size, &world, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	counts->worlds++;

	for (index = 0; index < world.model_count; index++)
		counts->faces +=
			(unsigned long)world.models[index].geometry.face_count;

	mh_world_free(&world);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_model(const struct mh_file *file, struct asset_counts *counts,
	struct mh_error *error_out)
{
	struct mh_model model;
	enum mh_error_code status;

	status = mh_model_myo_decode(file->data, file->size, &model, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	counts->faces += (unsigned long)model.face_count;
	counts->models++;
	mh_model_free(&model);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_archive(const struct mh_file *file, struct asset_counts *counts,
	struct mh_error *error_out)
{
	struct mh_pdi archive;
	struct mh_pdi_entry entry;
	struct mh_image image;
	struct mh_error error;
	enum mh_error_code status;
	unsigned long slot;

	status = mh_pdi_parse(file->data, file->size, &archive, &error);

	if (status != MH_ERROR_NONE) {
		return mh_error_forward(error_out, &error);
	}

	for (slot = 0; slot < archive.capacity; slot++) {
		if (mh_pdi_entry(&archive, slot, &entry)) {
			status = mh_image_ilbm_decode(
				entry.data, entry.size, &image, &error);

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "  entry %s: %s\n", entry.name,
					error.message);
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"PDI image validation failed");
			}

			free(image.rgba);
			counts->images++;
		}
	}

	counts->archives++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_image(const struct mh_file *file, struct asset_counts *counts,
	struct mh_error *error_out)
{
	struct mh_image image;
	enum mh_error_code status;

	status =
		mh_image_ilbm_decode(file->data, file->size, &image, error_out);

	if (status != MH_ERROR_NONE) {
		return status;
	}

	free(image.rgba);
	counts->images++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_asset(const struct mh_file *file, struct asset_counts *counts,
	struct mh_error *error_out)
{
	if (file->size >= 4 && memcmp(file->data, "!WYM", 4) == 0)
		return check_world(file, counts, error_out);

	if (file->size >= 4 &&
		(memcmp(file->data, "2OYM", 4) == 0 ||
			memcmp(file->data, "!OYM", 4) == 0))
		return check_model(file, counts, error_out);

	if (file->size >= 4 && memcmp(file->data, "PDI1", 4) == 0)
		return check_archive(file, counts, error_out);

	return check_image(file, counts, error_out);
}

static int
export_rgba(const char *input_path, const char *output_path)
{
	struct mh_file file;
	struct mh_image image;
	FILE *output;
	struct mh_error error;
	enum mh_error_code status;
	size_t size, written;
	int closed;

	status = mh_file_read(input_path, &file, &error);

	if (status == MH_ERROR_NONE) {
		status = mh_image_ilbm_decode(
			file.data, file.size, &image, &error);
		free(file.data);
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s: %s\n", input_path, error.message);
		return 1;
	}

	output = fopen(output_path, "wb");

	if (output == NULL) {
		fprintf(stderr, "Cannot create %s\n", output_path);
		free(image.rgba);
		return 1;
	}

	size = (size_t)image.width * image.height * RGBA_PIXEL_BYTES;
	written = fwrite(image.rgba, 1, size, output);
	closed = fclose(output);
	free(image.rgba);
	printf("%u x %u RGBA\n", image.width, image.height);

	return written == size && closed == 0 ? 0 : 1;
}

int
main(int argc, char *argv[])
{
	struct mh_file file;
	struct asset_counts counts;
	struct mh_error error;
	enum mh_error_code status;
	int failures, i;

	if (argc == 4 && strcmp(argv[1], "--rgba") == 0)
		return export_rgba(argv[2], argv[3]);

	if (argc < 2) {
		fprintf(stderr,
			"Usage: mh-asset-check FILE...\n"
			"       mh-asset-check --rgba IMAGE.IFF OUTPUT.rgba\n");
		return 1;
	}

	memset(&counts, 0, sizeof(counts));
	failures = 0;

	for (i = 1; i < argc; i++) {
		status = mh_file_read(argv[i], &file, &error);

		if (status == MH_ERROR_NONE) {
			status = check_asset(&file, &counts, &error);
			free(file.data);
		}

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "%s: %s\n", argv[i], error.message);
			failures++;
		}
	}

	printf("Validated %lu models, %lu worlds, %lu faces, %lu archives, %lu images; %d failures\n",
		counts.models, counts.worlds, counts.faces, counts.archives,
		counts.images, failures);

	return failures != 0;
}
