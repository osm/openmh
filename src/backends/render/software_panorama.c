#include <stdlib.h>
#include <string.h>

#include "backends/render/software_fog.h"
#include "backends/render/software_panorama.h"

static unsigned int
pack_color(const unsigned char rgb[3], unsigned int green_bits)
{
	return ((unsigned int)(rgb[0] >> 3) << (green_bits + 5)) |
		((unsigned int)(rgb[1] >> (8 - green_bits)) << 5) |
		(rgb[2] >> 3);
}

static enum mh_error_code
prepare_image(const struct mh_image *source, unsigned int width,
	unsigned int height, unsigned int green_bits,
	struct mh_software_sky_image *output, struct mh_error *error_out)
{
	unsigned char *bgr;
	size_t pixel, pixel_count;
	unsigned int image_width, image_height;
	enum mh_error_code status;

	status = mh_software_sky_size(source->width, source->height, width,
		height, &image_width, &image_height, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (image_height > MH_DEPTH_CUE_ROWS)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"panorama exceeds original fog table");

	pixel_count = (size_t)source->width * source->height;
	bgr = malloc(pixel_count * 3);

	if (bgr == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory converting panorama");

	for (pixel = 0; pixel < pixel_count; pixel++) {
		bgr[pixel * 3] = source->rgba[pixel * 4 + 2];
		bgr[pixel * 3 + 1] = source->rgba[pixel * 4 + 1];
		bgr[pixel * 3 + 2] = source->rgba[pixel * 4];
	}

	status = mh_software_sky_resize(bgr, pixel_count * 3, source->width,
		source->height, image_width, image_height, green_bits, output,
		error_out);
	free(bgr);

	return status;
}

enum mh_error_code
mh_software_panorama_set(struct mh_software_panorama *sky,
	const struct mh_panorama *settings, unsigned int width,
	unsigned int height, unsigned int green_bits,
	struct mh_error *error_out)
{
	struct mh_software_panorama next;
	unsigned int channel;
	enum mh_error_code status;

	next = *sky;
	memset(&next.image, 0, sizeof(next.image));
	next.enabled = settings != NULL;
	next.height_cue = settings != NULL && settings->height_cue;

	if (settings != NULL) {
		next.above = pack_color(settings->above, green_bits);
		next.below = pack_color(settings->below, green_bits);
		next.clear = pack_color(settings->clear, green_bits);

		for (channel = 0; channel < 3; channel++) {
			next.gradient.above[channel] = settings->above[channel];
			next.gradient.below[channel] = settings->below[channel];
		}
	}

	if (settings != NULL && settings->image != NULL) {
		status = mh_software_sky_horizon(
			height, settings->horizon, &next.horizon, error_out);

		if (status == MH_ERROR_NONE)
			status = prepare_image(settings->image, width, height,
				green_bits, &next.image, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_software_fog_rows(next.image.pixels,
				next.image.size, next.image.width,
				next.image.height, green_bits, &next.gradient,
				error_out);

		if (status != MH_ERROR_NONE) {
			free(next.image.pixels);
			return status;
		}

		/* Reload resets scrolling, but not fog top. */
		next.state.angle = next.state.phase = 0;
	}

	free(sky->image.pixels);
	*sky = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
