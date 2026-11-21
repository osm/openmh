#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "engine/assets/prepare_demo_loading.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "engine/assets/tga.h"
#include "engine/ui/loading_art.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "demo loading line %d: %s\n",          \
				__LINE__, #expression);                        \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_resource_entry entries[2];
	const struct mh_resource_source *source;
	const struct mh_user_data *data;
	struct mh_demo_recipe recipe;
	struct mh_file file;
	struct mh_image image;
	struct mh_indexed_image indexed;
	unsigned int frame;
	size_t pixel;

	memset(&recipe, 0, sizeof(recipe));
	recipe.track_layout = MH_DEMO_TRACK_SOFTWARE_FIRST;
	entries[0].path = "root/data/font1.fnt";
	entries[0].data = (const unsigned char *)"unused";
	entries[0].size = 6;
	CHECK(mh_memory_resources_open(entries, 1, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_loading_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, "root/data/Loading.TGA", 800000, &file,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_image_tga_decode(file.data, file.size, 0, &image, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_loading_background_check(&image, NULL) == MH_ERROR_NONE);
	CHECK(image.width == 640 && image.height == 400);
	CHECK(image.rgba[0] == 28 && image.rgba[1] == 33 &&
		image.rgba[2] == 42);
	CHECK(image.rgba[100 * 640 * 4] == 14);
	free(image.rgba);
	free(file.data);
	CHECK(mh_user_data_read(data, "root/data/LoadAnim.tga", 2048, &file,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_image_tga_decode_indexed(
		file.data, file.size, &indexed, NULL) == MH_ERROR_NONE);
	CHECK(mh_loading_indicators_prepare(&indexed, &image, NULL) ==
		MH_ERROR_NONE);
	free(indexed.indices);
	CHECK(image.width == 12 && image.height == 33);

	for (frame = 0; frame < 3; frame++) {
		CHECK(image.rgba[frame * 11 * 12 * 4 + 3] == 0);
		pixel = ((frame * 11 + 2) * 12 + 2) * 4;
		CHECK(image.rgba[pixel] == 255 &&
			image.rgba[pixel + 1] == 187 &&
			image.rgba[pixel + 2] == 66 &&
			image.rgba[pixel + 3] == 255);
	}

	free(image.rgba);
	free(file.data);
	mh_memory_resources_close(source);
	mh_memory_user_data_close(data);

	/* Refuse either existing name before creating the other output. */
	entries[1].path = "root/data/loadanim.TGA";
	entries[1].data = (const unsigned char *)"original";
	entries[1].size = 8;
	CHECK(mh_memory_resources_open(entries, 2, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_loading_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_EXISTS);
	CHECK(mh_user_data_read(data, "root/data/Loading.TGA", 800000, &file,
		NULL) == MH_ERROR_NOT_FOUND);
	mh_memory_resources_close(source);

	recipe.track_layout = MH_DEMO_TRACK_MPLAYER;
	CHECK(mh_memory_resources_open(NULL, 0, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_loading_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, "root/data/Loading.TGA", 800000, &file,
		NULL) == MH_ERROR_NOT_FOUND);
	mh_memory_resources_close(source);
	mh_memory_user_data_close(data);

	return 0;
}
