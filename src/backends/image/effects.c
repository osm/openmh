#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/effects.h"
#include "backends/image/provider.h"
#include "engine/assets/path_effects.h"
#include "engine/assets/tga.h"
#include "engine/core/text.h"
#include "engine/image/load.h"

struct effect_animation {
	const char	*hardware, *software;
	unsigned int	count, first, frames, size;
};

/* Keep the source palette so PowerVR can use its indexed lamp preparation. */
static enum mh_error_code
resize_indexed(const struct mh_indexed_image *source, unsigned int size,
	struct mh_file *output, struct mh_error *error_out)
{
	struct mh_indexed_image resized;
	unsigned int x, y;
	enum mh_error_code status;

	resized = *source;
	resized.width = resized.height = size;
	resized.size = (size_t)size * size;
	resized.indices = malloc(resized.size);

	if (resized.indices == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory adapting effect artwork");

	for (y = 0; y < size; y++)
		for (x = 0; x < size; x++)
			resized.indices[(size_t)y * size + x] =
				source->indices[((size_t)y * source->height /
							size) *
						source->width +
					(size_t)x * source->width / size];

	status = mh_image_tga_encode_indexed(&resized, output, error_out);
	free(resized.indices);

	return status;
}

static int
source_image(const char *reference, char path[128], unsigned int *size)
{
	static const struct effect_animation animations[] = {
		{MH_ASSET_HARDWARE_SMOKE_FORMAT, MH_ASSET_SOFTWARE_SMOKE_FORMAT,
			9, 21, 17, 32},
		{MH_ASSET_HARDWARE_GRASS_FORMAT, MH_ASSET_SOFTWARE_GRASS_FORMAT,
			9, 40, 17, 32},
		{MH_ASSET_HARDWARE_SPARK_FORMAT, MH_ASSET_SOFTWARE_SPARK_FORMAT,
			6, 20, 20, 64}};

	char name[128];
	unsigned int group, frame;

	if (mh_ascii_equal(reference, MH_ASSET_LAMP_GLOW) ||
		mh_ascii_equal(reference, MH_ASSET_LAMP_FLARE)) {
		*size = mh_ascii_equal(reference, MH_ASSET_LAMP_GLOW) ? 128
								: 64;
		strcpy(path,
			*size == 128 ? MH_ASSET_SOFTWARE_GLOW
				: MH_ASSET_SOFTWARE_HALO);
		return 1;
	}

	for (group = 0; group < sizeof(animations) / sizeof(animations[0]);
		group++)
		for (frame = 0; frame < animations[group].count; frame++) {
			sprintf(name, animations[group].hardware, frame + 1);

			if (!mh_ascii_equal(reference, name))
				continue;

			sprintf(path, animations[group].software,
				animations[group].first +
					frame * (animations[group].frames - 1) /
						(animations[group].count - 1));
			*size = animations[group].size;
			return 1;
		}

	return 0;
}

enum mh_error_code
mh_image_effect_fallback(const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_indexed_image image;
	char path[128];
	unsigned int size;
	enum mh_error_code status;

	if (!source_image(reference, path, &size))
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"no alternate effect artwork");

	status = mh_image_load_indexed_from(mh_image_fallback_decoder(),
		resources, root, path, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = resize_indexed(&image, size, output, error_out);
	free(image.indices);

	return status;
}
