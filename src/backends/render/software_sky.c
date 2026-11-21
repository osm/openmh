#include <math.h>
#include <string.h>

#include "backends/render/software_sky.h"
#include "engine/core/limits.h"

#define VIEW_LIMIT 4096
#define PITCH_DEAD_ZONE .001
#define HALF_PI 1.570796326795
#define PI_F32 3.1415927410125732f
#define FOV_SCALE 114.5915590261571

enum mh_error_code
mh_software_sky_horizon(unsigned int height, float horizon, int *offset,
	struct mh_error *error_out)
{
	if (offset == NULL || height == 0 || height > VIEW_LIMIT ||
		!(horizon >= 0 && horizon <= 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software sky horizon");

	*offset = (int)((double)height * horizon) - (int)(height / 2);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static double
direction_angle(float x, float z)
{
	const float zero = 0;
	double angle;

	if (x != 0)
		return atan((double)z / x);

	angle = z < 0 ? -2 * atan(1.0) : 2 * atan(1.0);

	return memcmp(&x, &zero, sizeof(x)) == 0 ? angle : -angle;
}

static int
place_image(const struct mh_software_surface *surface,
	const struct mh_software_sky_image *image,
	const struct mh_software_sky_camera *camera,
	struct mh_software_sky_state *state)
{
	double fov, raw_angle, change;
	float scale, pitch, angle, delta, alternate, phase;
	int top;

	fov = atan(1.0 / camera->focal) * FOV_SCALE;
	scale = (float)(180.0 / fov);
	scale = (float)(((double)surface->height / surface->width) * scale *
		.5);
	pitch = fabs((double)camera->forward[1]) < PITCH_DEAD_ZONE
		? 0
		: camera->forward[1];
	pitch = (float)((double)surface->height * scale * pitch);
	top = (int)(surface->height * .5 + pitch) - (int)(image->height / 2);
	top += camera->horizon_offset;
	state->top = (float)top;

	/*
	 * Supercars looks straight down. Original 0/0 yaw skips panorama
	 * copies after its indefinite integer conversion; edge fills remain.
	 * Retain finite yaw history instead of publishing the original NaNs.
	 */
	if (camera->forward[0] == 0 && camera->forward[2] == 0)
		return 0;

	raw_angle = direction_angle(camera->forward[0], camera->forward[2]);
	angle = (float)(raw_angle + HALF_PI);
	delta = (float)(raw_angle + HALF_PI - state->angle);
	alternate = state->angle > HALF_PI
		? (float)((double)PI_F32 - state->angle + angle)
		: (float)(-(double)state->angle - ((double)PI_F32 - angle));

	if (fabs((double)alternate) <= fabs((double)delta))
		delta = alternate;

	change = ((double)delta / PI_F32) *
		((double)surface->width * (180.0 / (float)fov));
	phase = (float)(state->phase + change);
	phase = (float)fmod(phase, image->width);

	if (phase < 0)
		phase = (float)((double)phase + image->width);

	state->angle = angle;
	state->phase = phase;

	return 1;
}

static void
fill_rows(const struct mh_software_surface *surface, unsigned int first,
	unsigned int end, unsigned int color, enum mh_software_sky_mode mode)
{
	unsigned char *pixel, *stop;
	unsigned int row;

	for (row = first; row < end; row++) {
		pixel = surface->pixels + (size_t)row * surface->pitch;
		stop = pixel +
			(mode == MH_SOFTWARE_SKY_HISTORY
					? surface->pitch
					: (size_t)surface->width * 2);

		while (pixel < stop) {
			*pixel++ = (unsigned char)color;
			*pixel++ = (unsigned char)(color >> 8);
		}
	}
}

static void
copy_rows(const struct mh_software_surface *surface,
	const struct mh_software_sky_image *image, int top, unsigned int phase,
	unsigned int above, unsigned int below, enum mh_software_sky_mode mode,
	int copy_image)
{
	unsigned int row, column, source_x, source_y, count, end, cropped;
	unsigned char *destination;
	const unsigned char *source;
	int bottom;

	cropped = top < 0 ? (unsigned int)-top : 0;
	row = top < 0 ? 0 : (unsigned int)top;

	if (row > surface->height)
		row = surface->height;

	fill_rows(surface, 0, row, above, mode);
	end = row + image->height;

	if (end > surface->height)
		end = surface->height;

	/*
	 * Original clips before subtracting the source crop, even for tall
	 * skies.
	 */
	end = end > cropped ? end - cropped : 0;
	source_y = cropped;

	for (; copy_image && row < end; row++, source_y++) {
		destination = surface->pixels + (size_t)row * surface->pitch;
		source = image->pixels + (size_t)source_y * image->width * 2;
		source_x = (image->width - phase) % image->width;

		for (column = 0; column < surface->width; column += count) {
			count = image->width - source_x;

			if (count > surface->width - column)
				count = surface->width - column;

			memcpy(destination + (size_t)column * 2,
				source + (size_t)source_x * 2,
				(size_t)count * 2);
			source_x = 0;
		}
	}

	row = top < 0 ? 0 : (unsigned int)top;

	if (row > surface->height)
		row = surface->height;

	if (row + image->height < surface->height) {
		bottom = (int)(row + image->height) - (int)cropped;
		fill_rows(surface, bottom < 0 ? 0 : (unsigned int)bottom,
			surface->height, below, mode);
	}
}

enum mh_error_code
mh_software_sky_draw(const struct mh_software_surface *surface,
	const struct mh_software_sky_image *image,
	const struct mh_software_sky_camera *camera,
	struct mh_software_sky_state *state, enum mh_software_sky_mode mode,
	unsigned int above, unsigned int below, unsigned int clear,
	struct mh_error *error_out)
{
	struct mh_software_sky_state next;
	unsigned int axis;
	int copy_image;

	if (surface == NULL || surface->pixels == NULL || camera == NULL ||
		state == NULL || surface->width == 0 ||
		surface->width > VIEW_LIMIT || surface->height == 0 ||
		surface->height > VIEW_LIMIT ||
		surface->pitch < (size_t)surface->width * 2 ||
		surface->pitch % 2 ||
		surface->pitch > (size_t)-1 / surface->height ||
		surface->size < surface->pitch * surface->height ||
		!(camera->focal >= .001f && camera->focal <= 100) ||
		camera->horizon_offset < -VIEW_LIMIT ||
		camera->horizon_offset > VIEW_LIMIT ||
		!(state->angle >= 0 && state->angle <= PI_F32) ||
		!(state->phase >= 0 && state->phase <= VIEW_LIMIT) ||
		above > MH_U16_MAX || below > MH_U16_MAX ||
		clear > MH_U16_MAX ||
		(mode != MH_SOFTWARE_SKY_DIRECT &&
			mode != MH_SOFTWARE_SKY_HISTORY))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software sky frame");

	for (axis = 0; axis < 3; axis++)
		if (!(camera->forward[axis] >= -1 &&
			camera->forward[axis] <= 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software sky direction");

	if (image == NULL) {
		fill_rows(surface, 0, surface->height,
			mode == MH_SOFTWARE_SKY_HISTORY ? 0 : clear, mode);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (image->pixels == NULL || image->width == 0 ||
		image->width > surface->width || image->height == 0 ||
		image->height > VIEW_LIMIT ||
		(size_t)image->width > (size_t)-1 / 2 / image->height ||
		image->size < (size_t)image->width * image->height * 2)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software sky image");

	next = *state;
	copy_image = place_image(surface, image, camera, &next);
	copy_rows(surface, image, (int)next.top, (unsigned int)next.phase,
		above, below, mode, copy_image);
	*state = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
