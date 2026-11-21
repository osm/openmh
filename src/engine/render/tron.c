#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/clip_codes.h"
#include "engine/render/pixel_snap.h"
#include "engine/render/tron.h"

#define VIEWPORT_LIMIT 16384
#define ATTRIBUTE_LIMIT 1e9f
#define TEXTURE_LIMIT 1e6f
#define DEPTH_SCALE 65536.0f
#define TEXTURE_SCALE 256.0f
#define COLOR_BYTE_MAX 255
#define BLUE_MEAN_SCALE .33
#define ALPHA_BLUE_SCALE .25

static int
bounded_attribute(float value)
{
	return value >= -ATTRIBUTE_LIMIT && value <= ATTRIBUTE_LIMIT;
}

static unsigned int
outcode(const struct mh_tron_line_vertex *vertex, unsigned int width,
	unsigned int height)
{
	return (vertex->xy[0] < 0 ? MH_CLIP_LEFT : 0U) |
		(vertex->xy[0] >= width ? MH_CLIP_RIGHT : 0U) |
		(vertex->xy[1] < 0 ? MH_CLIP_TOP : 0U) |
		(vertex->xy[1] >= height ? MH_CLIP_BOTTOM : 0U);
}

static float
interpolate(float a, float b, long double t)
{
	return (float)(((long double)b - a) * t + a);
}

static int
intersection(const struct mh_tron_line_vertex input[2], unsigned int axis,
	float boundary, struct mh_tron_line_vertex *vertex)
{
	float numerator;
	long double denominator, t;
	unsigned int coord, channel;

	denominator = (long double)input[1].xy[axis] - input[0].xy[axis];

	if (denominator == 0)
		return 0;

	/* The numerator is explicitly stored as binary32 before division. */
	numerator = (float)((long double)boundary - input[0].xy[axis]);
	t = numerator / denominator;

	for (coord = 0; coord < 2; coord++)
		vertex->xy[coord] = mh_pixel_snap(
			((long double)input[1].xy[coord] - input[0].xy[coord]) *
				t +
			input[0].xy[coord]);

	for (channel = 0; channel < 3; channel++)
		vertex->rgb[channel] = interpolate(
			input[0].rgb[channel], input[1].rgb[channel], t);

	vertex->depth = interpolate(input[0].depth, input[1].depth, t);
	vertex->alpha = interpolate(input[0].alpha, input[1].alpha, t);
	vertex->reciprocal_w =
		interpolate(input[0].reciprocal_w, input[1].reciprocal_w, t);

	return 1;
}

enum mh_error_code
mh_tron_clip_line(unsigned int width, unsigned int height,
	const struct mh_tron_line_vertex input[2],
	struct mh_tron_line_vertex output[2], int *visible,
	struct mh_error *error_out)
{
	struct mh_tron_line_vertex clipped[2];
	unsigned int codes[2], end, axis, channel;

	if (input == NULL || output == NULL || visible == NULL || width < 1 ||
		width > VIEWPORT_LIMIT || height < 1 || height > VIEWPORT_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid TRON line viewport");

	for (end = 0; end < 2; end++) {
		for (axis = 0; axis < 2; axis++)
			if (!bounded_attribute(input[end].xy[axis]))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid TRON line position");

		for (channel = 0; channel < 3; channel++)
			if (!bounded_attribute(input[end].rgb[channel]))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid TRON line color");

		if (!bounded_attribute(input[end].depth) ||
			!bounded_attribute(input[end].alpha) ||
			!bounded_attribute(input[end].reciprocal_w))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid TRON line projection");

		clipped[end] = input[end];
		codes[end] = outcode(&input[end], width, height);
	}

	if ((codes[0] | codes[1]) != 0) {
		for (end = 0; end < 2; end++)
			for (axis = 0; axis < 2; axis++)
				clipped[end].xy[axis] =
					mh_pixel_snap(input[end].xy[axis]);

		for (end = 0; end < 2; end++) {
			if (codes[end] & (MH_CLIP_LEFT | MH_CLIP_RIGHT)) {
				if (!intersection(input, 0,
					codes[end] & MH_CLIP_LEFT
						? 0
						: (float)width,
					&clipped[end])) {
					*visible = 0;
					return mh_error_set(
						error_out, MH_ERROR_NONE, NULL);
				}

				codes[end] =
					outcode(&clipped[end], width, height);

				if (codes[end] & codes[1 - end]) {
					*visible = 0;
					return mh_error_set(
						error_out, MH_ERROR_NONE, NULL);
				}
			}

			if (codes[end] & (MH_CLIP_TOP | MH_CLIP_BOTTOM)) {
				if (!intersection(input, 1,
					codes[end] & MH_CLIP_TOP
						? 0
						: (float)height,
					&clipped[end])) {
					*visible = 0;
					return mh_error_set(
						error_out, MH_ERROR_NONE, NULL);
				}

				codes[end] =
					outcode(&clipped[end], width, height);

				if (codes[end] & codes[1 - end]) {
					*visible = 0;
					return mh_error_set(
						error_out, MH_ERROR_NONE, NULL);
				}
			}
		}
	}

	output[0] = clipped[0];
	output[1] = clipped[1];
	*visible = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_tron_style *
mh_tron_style(enum mh_scene_pass pass)
{
	static const struct mh_tron_style styles[MH_SCENE_PASSES] = {
		{MH_TRON_BACKGROUND, 0, 0, 1, 0},
		{MH_TRON_SOURCE_ALPHA, 0, 1, 1, 0},
		{MH_TRON_REPLACE, 1, 0, 0, 10}, {MH_TRON_REPLACE, 1, 0, 0, 30},
		{MH_TRON_REPLACE, 1, 0, 0, 10}, {MH_TRON_REPLACE, 1, 1, 0, 0},
		{MH_TRON_ADD, 0, 0, 0, 10}};

	return pass >= MH_SCENE_DISTANT_SOLID && pass < MH_SCENE_PASSES
		? &styles[pass]
		: NULL;
}

enum mh_error_code
mh_tron_prepare_face(enum mh_scene_pass pass, unsigned int count, int smooth,
	const struct mh_scene_vertex *corners, struct mh_tron_face *output,
	struct mh_error *error_out)
{
	struct mh_tron_face face;
	struct mh_tron_vertex *vertex;
	const struct mh_scene_vertex *source;
	const float *rgb;
	long double blue;
	unsigned int corner, axis, channel;
	int textured;

	if (mh_tron_style(pass) == NULL || corners == NULL || output == NULL ||
		(count != 3 && count != 4) || (smooth != 0 && smooth != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid TRON face");

	textured = pass == MH_SCENE_DISTANT_ALPHA ||
		pass == MH_SCENE_TEXTURED || pass == MH_SCENE_ENVIRONMENT ||
		pass == MH_SCENE_ALPHA;
	memset(&face, 0, sizeof(face));
	face.count = count;
	face.pass = pass;

	for (corner = 0; corner < count; corner++) {
		source = &corners[corner];

		if (!(source->depth >= 0 && source->depth <= 1) ||
			source->visibility > COLOR_BYTE_MAX ||
			source->background > COLOR_BYTE_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid TRON face depth/alpha");

		for (axis = 0; axis < 2; axis++)
			if (!bounded_attribute(source->xy[axis]))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid TRON face position");

		for (channel = 0; channel < 3; channel++)
			if (!(source->rgb[channel] >= 0 &&
				source->rgb[channel] <= COLOR_BYTE_MAX))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid TRON face color");

		vertex = &face.vertices[corner];

		for (axis = 0; axis < 2; axis++)
			vertex->line.xy[axis] = mh_pixel_snap(source->xy[axis]);

		vertex->line.depth =
			(float)((long double)source->depth * DEPTH_SCALE);
		vertex->line.alpha = (float)(pass == MH_SCENE_DISTANT_SOLID
				? source->background
				: COLOR_BYTE_MAX -
					(pass == MH_SCENE_DISTANT_ALPHA
							? source->background
							: source->visibility));

		if (pass >= MH_SCENE_TEXTURED) {
			if ((pass == MH_SCENE_TEXTURED ||
				pass == MH_SCENE_UNAVAILABLE) &&
				!smooth)
				rgb = corners[0].rgb;
			else
				rgb = source->rgb;

			blue = ((long double)rgb[0] + rgb[1] + rgb[2]) *
				(double)BLUE_MEAN_SCALE;

			if (pass == MH_SCENE_ALPHA)
				blue *= (double)ALPHA_BLUE_SCALE;

			vertex->line.rgb[2] = (float)blue;
		}

		if (textured) {
			if (!(source->reciprocal_w > 0 &&
				source->reciprocal_w <= TEXTURE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid TRON face reciprocal W");

			vertex->line.reciprocal_w = source->reciprocal_w;

			for (axis = 0; axis < 2; axis++) {
				if (!(source->uv[axis] >= -TEXTURE_LIMIT &&
					source->uv[axis] <= TEXTURE_LIMIT))
					return mh_error_set(error_out,
						MH_ERROR_INVALID,
						"invalid TRON face texture coordinate");

				vertex->texture_w[axis] =
					(float)((long double)source->uv[axis] *
						TEXTURE_SCALE *
						source->reciprocal_w);
			}
		} else {
			/*
			 * Original untextured passes neither set nor consume W.
			 */
			vertex->line.reciprocal_w = 1;
		}
	}

	*output = face;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_tron_face_outline(const struct mh_tron_face *face, unsigned int width,
	unsigned int height, struct mh_tron_line_vertex output[8],
	unsigned int *count, struct mh_error *error_out)
{
	struct mh_tron_line_vertex lines[8], input[2];
	const struct mh_tron_style *style;
	enum mh_error_code status;
	unsigned int edge, end, line_count;
	int visible;

	if (face == NULL || output == NULL || count == NULL || width < 1 ||
		width > VIEWPORT_LIMIT || height < 1 ||
		height > VIEWPORT_LIMIT ||
		(face->count != 3 && face->count != 4))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid TRON outline");

	style = mh_tron_style(face->pass);

	if (style == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid TRON outline pass");

	line_count = 0;

	if (style->outline_bias != 0)
		for (edge = 0; edge < face->count; edge++) {
			input[0] = face->vertices[edge].line;
			input[1] =
				face->vertices[(edge + 1) % face->count].line;

			for (end = 0; end < 2; end++) {
				input[end].rgb[0] = input[end].rgb[1] = 0;
				input[end].rgb[2] = COLOR_BYTE_MAX;
			}

			status = mh_tron_clip_line(width, height, input,
				lines + line_count * 2, &visible, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (visible)
				line_count++;
		}

	if (line_count != 0)
		memcpy(output, lines, line_count * 2 * sizeof(*lines));

	*count = line_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
