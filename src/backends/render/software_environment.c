#include <stdlib.h>
#include <string.h>

#include "backends/render/software_environment.h"

#define MAP_EDGE 256

void
mh_software_environment_free(struct mh_software_environment *environment)
{
	unsigned int map;

	for (map = 0; map < 3; map++)
		free(environment->maps[map].indices);

	memset(environment, 0, sizeof(*environment));
}

enum mh_error_code
mh_software_environment_set(struct mh_software_environment *environment,
	const struct mh_indexed_image *const maps[3],
	struct mh_error *error_out)
{
	struct mh_software_environment next;
	const struct mh_indexed_image *image;
	size_t size, pixel;
	unsigned int map;

	if (maps == NULL) {
		mh_software_environment_free(environment);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	size = (size_t)MAP_EDGE * MAP_EDGE;

	for (map = 0; map < 3; map++) {
		image = maps[map];

		if (image == NULL || image->indices == NULL ||
			image->width != MAP_EDGE || image->height != MAP_EDGE ||
			image->size < size || image->colors == 0 ||
			image->colors > 256)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"software reflection maps require 256x256 indexed images");

		for (pixel = 0; pixel < size; pixel++)
			if (image->indices[pixel] >= image->colors)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"software reflection index outside palette");
	}

	memset(&next, 0, sizeof(next));

	for (map = 0; map < 3; map++) {
		next.maps[map] = *maps[map];
		next.maps[map].indices = malloc(size);

		if (next.maps[map].indices == NULL) {
			mh_software_environment_free(&next);
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory copying software reflection maps");
		}

		next.maps[map].size = size;
		memcpy(next.maps[map].indices, maps[map]->indices, size);
	}

	next.loaded = 1;
	mh_software_environment_free(environment);
	*environment = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
