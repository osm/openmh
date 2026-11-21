#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/sky.h"

#define VIEWPORT_LIMIT 20000
#define IMAGE_HEIGHT_LIMIT 4096
#define ANGLE_LIMIT 10
#define FOCAL_LIMIT 100
#define YAW_SCROLL_SCALE 1.8
#define PITCH_SCALE 1.7999999523162842
#define PITCH_DEADZONE .001
#define HALF_PI 1.570796326795
#define PI_F32 3.1415927410125732f

static int
bounded(float value, float limit)
{
	return value >= -limit && value <= limit;
}

enum mh_error_code
mh_sky_quads(struct mh_sky_state *state, const struct mh_sky_camera *camera,
	struct mh_effect_quad output[MH_SKY_QUADS], struct mh_error *error_out)
{
	struct mh_sky_state next;
	struct mh_effect_quad quads[MH_SKY_QUADS], *quad;
	float angle, delta, alternate, phase, pitch, top, bottom, left, right,
		edges[4], edge, cosine, offset_x, offset_y;
	unsigned int axis, layer, corner;
	double raw_angle;

	if (state == NULL || camera == NULL || output == NULL ||
		!bounded(state->angle, ANGLE_LIMIT) ||
		!(state->phase >= 0 && state->phase < 1) ||
		!(camera->width > 0 && camera->width <= VIEWPORT_LIMIT) ||
		!(camera->height > 0 && camera->height <= VIEWPORT_LIMIT) ||
		!(camera->focal > 0 && camera->focal <= FOCAL_LIMIT) ||
		!bounded(camera->roll_sine, 1) ||
		!(camera->horizon >= 0 && camera->horizon <= 1) ||
		!(camera->image_scale > 0 &&
			camera->image_scale <= VIEWPORT_LIMIT) ||
		!(camera->image_height == 0 ||
			(camera->image_height >= 2 &&
				camera->image_height <= IMAGE_HEIGHT_LIMIT)))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid sky camera");

	for (axis = 0; axis < 3; axis++)
		if (!bounded(camera->forward[axis], 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid sky direction");

	/*
	 * Atan(forward.z/forward.x), with a pi-periodic seam, accumulated into
	 * U.
	 */
	raw_angle = camera->forward[0] == 0
		? (camera->forward[2] < 0 ? -HALF_PI : HALF_PI)
		: atan((double)camera->forward[2] / camera->forward[0]);

	angle = (float)(raw_angle + HALF_PI);
	delta = (float)(raw_angle + HALF_PI - state->angle);
	alternate = state->angle <= HALF_PI ? angle - state->angle - PI_F32
					: angle - state->angle + PI_F32;

	if (fabs((double)alternate) <= fabs((double)delta))
		delta = alternate;

	phase = (float)fmod((double)state->phase +
			(double)delta / PI_F32 * YAW_SCROLL_SCALE,
		1);

	if (phase < 0)
		phase += 1;

	if (phase >= 1)
		phase = 0;

	next.angle = angle;
	next.phase = phase;
	pitch = fabs((double)camera->forward[1]) < PITCH_DEADZONE
		? 0
		: camera->forward[1];
	top = (float)(camera->height * .5 +
		camera->height *
			(float)(camera->height / camera->width * PITCH_SCALE *
				.5) *
			pitch -
		camera->image_height * .5 * camera->image_scale +
		(camera->image_height == 0
				? 0
				: (int)(camera->height * camera->horizon) -
					(int)((unsigned int)camera->height /
						2)));
	bottom = top + camera->image_height * camera->image_scale;
	cosine = (float)sqrt(1 - (double)camera->roll_sine * camera->roll_sine);
	left = -camera->width;
	right = camera->width * 2;
	edges[0] = top;
	edges[1] = bottom;
	edges[2] = -camera->height;
	edges[3] = camera->height * 2;
	memset(quads, 0, sizeof(quads));

	for (layer = 0; layer < MH_SKY_QUADS; layer++) {
		quad = &quads[layer];

		for (corner = 0; corner < 4; corner++) {
			offset_x =
				((corner == 0 || corner == 3 ? left : right) -
					camera->width * .5f) *
				camera->focal;

			if (layer == MH_SKY_PANORAMA)
				edge = edges[corner < 2 ? 0 : 1];
			else if (layer == MH_SKY_TOP_FILL)
				edge = edges[corner < 2 ? 2 : 0];
			else
				edge = edges[corner < 2 ? 1 : 3];

			offset_y = edge - camera->height * .5f;

			/*
			 * Fill extends to viewport overscan; only the sky edge
			 * scales with focal.
			 */
			if (layer == MH_SKY_PANORAMA ||
				(layer == MH_SKY_TOP_FILL && corner >= 2) ||
				(layer == MH_SKY_BOTTOM_FILL && corner < 2))
				offset_y *= camera->focal;

			quad->xy[corner][0] = camera->width * .5f +
				offset_x * cosine -
				offset_y * camera->roll_sine;
			quad->xy[corner][1] = camera->height * .5f +
				offset_x * camera->roll_sine +
				offset_y * cosine;
			quad->uv[corner][0] =
				(corner == 0 || corner == 3 ? 0 : 3) - phase;

			/*
			 * SkyImage Off leaves the original texture height zero.
			 * Those infinite V coordinates are unused without a
			 * texture; keep our untextured fill finite instead.
			 */
			quad->uv[corner][1] = camera->image_height == 0 ? 0
				: corner < 2 ? 1 / camera->image_height
					: (camera->image_height - 1) /
					camera->image_height;
		}
	}

	*state = next;
	memcpy(output, quads, sizeof(quads));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
