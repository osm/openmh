#include <math.h>
#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/pose.h"
#include "engine/render/retro.h"

#define PIXEL_SNAP_BIAS 422212465065984.0L
#define VIEWPORT_LIMIT 16384
#define FONT_HEIGHT_LIMIT 256
#define TIME_LIMIT 1e6f
#define FRAME_TIME_LIMIT 60
#define SCROLL_LINE_STEP .25f
#define BACKDROP_DEPTH 63000
#define STORY_DEPTH 64000
#define MESH_DEPTH_CENTER 60000.0f
#define MESH_DEPTH_SCALE 1000.0f
#define COLOR_BYTE_MAX 255
#define COLOR_BYTE_RANGE 256
/* These are the original decimal approximations, not full-precision pi. */
#define GLYPH_PI 3.141592
#define GLYPH_TURN 6.283184

static float
snap_pixel(long double value)
{
	volatile long double sum;

	sum = value + PIXEL_SNAP_BIAS;

	return (float)(sum - PIXEL_SNAP_BIAS);
}

enum mh_error_code
mh_retro_backdrop(unsigned int width, unsigned int height, float center,
	struct mh_retro_vertex output[MH_RETRO_BACKDROP_VERTICES],
	struct mh_error *error_out)
{
	static const unsigned int forward[MH_RETRO_QUAD_VERTICES] = {
		0, 1, 2, 0, 2, 3};

	static const unsigned int reverse[MH_RETRO_QUAD_VERTICES] = {
		3, 2, 1, 3, 1, 0};

	struct mh_retro_vertex vertices[MH_RETRO_BACKDROP_VERTICES], quad[4];
	const unsigned int *order;
	double lower;
	float edges[7], top, bottom, alpha_top, alpha_bottom;
	unsigned int layer, corner;

	if (width < 1 || width > VIEWPORT_LIMIT || height < 1 ||
		height > VIEWPORT_LIMIT || !(center >= 0 && center <= 1) ||
		output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid retro backdrop");

	lower = (double)((long double)center + (double)-.1);
	edges[0] = (float)(((long double)lower + (double)-.02) * height);
	edges[1] = (float)((long double)lower * height);
	edges[2] = (float)(((long double)center + (double).1) * height);
	edges[3] = (float)((((long double)center + (double).1) + (double).02) *
		height);
	edges[4] = (float)((((long double)center + (double)-.1) + (double).01) *
		height);
	edges[5] = (float)((long double)center * height);
	edges[6] = (float)((((long double)center + (double).1) + (double)-.01) *
		height);

	for (layer = 0; layer < MH_RETRO_BACKDROP_QUADS; layer++) {
		memset(quad, 0, sizeof(quad));
		order = layer == 1 || layer == 4 ? reverse : forward;
		top = layer == 0		   ? edges[0]
			: layer == 1 || layer == 2 ? edges[2]
			: layer == 3		   ? edges[4]
						: edges[6];
		bottom = layer < 2   ? edges[1]
			: layer == 2 ? edges[3]
				: edges[5];
		alpha_top = layer == 0 ? 0 : layer < 3 ? COLOR_BYTE_MAX : 32;
		alpha_bottom = layer == 2 ? 0 : COLOR_BYTE_MAX;

		for (corner = 0; corner < 4; corner++) {
			quad[corner].xy[0] =
				corner == 1 || corner == 2 ? (float)width : 0;
			quad[corner].xy[1] = corner < 2 ? top : bottom;
			quad[corner].alpha =
				corner < 2 ? alpha_top : alpha_bottom;
			quad[corner].depth =
				layer < 3 ? BACKDROP_DEPTH : STORY_DEPTH;
			quad[corner].rgb[2] = layer < 3 ? 0 : COLOR_BYTE_MAX;
		}

		for (corner = 0; corner < MH_RETRO_QUAD_VERTICES; corner++)
			vertices[layer * MH_RETRO_QUAD_VERTICES + corner] =
				quad[order[corner]];
	}

	memcpy(output, vertices, sizeof(vertices));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_retro_glyph(unsigned int width, unsigned int height,
	unsigned int font_height, float center, float scroll, float wave,
	const float xy[4][2], struct mh_retro_vertex output[4],
	struct mh_error *error_out)
{
	struct mh_retro_vertex vertices[4], *vertex;
	unsigned int corner, axis, channel;
	double twist;
	long double angle, bend;
	float stored_angle, brightness, y;

	if (width < 1 || width > VIEWPORT_LIMIT || height < 1 ||
		height > VIEWPORT_LIMIT || font_height < 1 ||
		font_height > FONT_HEIGHT_LIMIT || xy == NULL ||
		output == NULL || !(center >= 0 && center <= 1) ||
		!(scroll >= -.75f && scroll <= 1) ||
		!(wave >= 0 && wave <= TIME_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid retro glyph request");

	for (corner = 0; corner < 4; corner++)
		for (axis = 0; axis < 2; axis++)
			if (!(xy[corner][axis] >= -VIEWPORT_LIMIT &&
				xy[corner][axis] <= VIEWPORT_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid retro glyph corner");

	memset(vertices, 0, sizeof(vertices));

	for (corner = 0; corner < 4; corner++) {
		vertex = &vertices[corner];

		twist = (double)(((long double)xy[corner][0] * (double).15 *
						width * (double).00125 +
					xy[corner][1]) *
			(double)GLYPH_TURN);
		angle = (long double)twist / font_height * (double).125 +
			(double)GLYPH_PI -
			(long double)scroll * 2.0f * (double)GLYPH_PI;
		bend = (long double)width * (float).00125f * xy[corner][0] *
				(double).03 +
			wave;
		angle += (long double)sin((double)bend) * (double).5;
		stored_angle = (float)angle;

		/*
		 * Brightness consumes the extended angle left after the store;
		 * position and depth reload the stored binary32 value.
		 */
		brightness =
			(float)((long double)cos((double)angle) * (double).75 +
				(double).25);

		if (brightness < 0)
			brightness = 0;

		vertex->xy[0] = snap_pixel(
			(long double)xy[corner][0] * 3.0f + (width >> 1));
		y = (float)((long double)sin(stored_angle) * (double).1);
		y = (float)((long double)center + y);
		vertex->xy[1] = snap_pixel((long double)y * height);
		vertex->depth =
			(float)((long double)cos(stored_angle) * (double)100 +
				(double)STORY_DEPTH);
		vertex->alpha = COLOR_BYTE_MAX;

		for (channel = 0; channel < 3; channel++)
			vertex->rgb[channel] =
				(float)((long double)COLOR_BYTE_MAX *
					brightness);
	}

	memcpy(output, vertices, sizeof(vertices));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_retro_frame(float dt, const struct mh_retro_state *state,
	struct mh_retro_frame *output, struct mh_error *error_out)
{
	struct mh_retro_frame frame;
	float scroll, step;
	unsigned int line;

	if (state == NULL || output == NULL ||
		!(dt >= 0 && dt <= FRAME_TIME_LIMIT) ||
		state->line >= MH_RETRO_LINES - (MH_RETRO_VISIBLE_LINES - 1) ||
		!(state->scroll >= 0 && state->scroll <= 1) ||
		!(state->wave >= 0 && state->wave <= TIME_LIMIT) ||
		!(state->time >= 0 && state->time <= TIME_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid retro frame state");

	memset(&frame, 0, sizeof(frame));
	frame.next = *state;
	frame.center = (float)((long double)sin(state->time) * (double).05 +
		(double).85);
	frame.wave = state->wave;
	scroll = state->scroll;

	for (line = 0; line < MH_RETRO_VISIBLE_LINES; line++) {
		frame.lines[line] = state->line + line;
		frame.scroll[line] = scroll;
		scroll = (float)((long double)scroll - SCROLL_LINE_STEP);
	}

	scroll = (float)((long double)scroll + 1);
	step = (float)((long double)dt * (double).2);

	if (step > 1)
		step = 1;

	scroll = (float)((long double)step * (double).5 + scroll);

	while (scroll > 1) {
		frame.next.line++;
		scroll = (float)((long double)scroll - SCROLL_LINE_STEP);

		if (frame.next.line + (MH_RETRO_VISIBLE_LINES - 1) ==
			MH_RETRO_LINES)
			frame.next.line = 0;
	}

	frame.next.scroll = scroll;
	frame.next.wave = (float)((long double)dt * 2.0f + state->wave);
	frame.next.time = (float)((long double)dt + state->time);
	*output = frame;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_retro_polygons(const struct mh_retro_assets *assets, unsigned int instance,
	unsigned int width, unsigned int height, float time,
	struct mh_retro_polygon output[MH_RETRO_MAX_FACES], unsigned int *count,
	struct mh_error *error_out)
{
	struct mh_retro_polygon polygons[MH_RETRO_MAX_FACES], *polygon;
	const struct mh_retro_mesh *mesh;
	const struct mh_retro_face *face;
	struct mh_retro_vertex projected[MH_RETRO_MAX_POINTS], *vertex;
	double angles[3], matrix[9];
	float bob[3], rotated[3], screen_center[2];
	const float *point;
	const float (*motion)[3];
	long double normal_dot, shade, value, area;
	unsigned int axis, point_index, face_index, corner, channel,
		visible_count;
	enum mh_error_code status;

	if (assets == NULL || output == NULL || count == NULL ||
		instance >= MH_RETRO_INSTANCE_COUNT || width < 1 ||
		width > VIEWPORT_LIMIT || height < 1 ||
		height > VIEWPORT_LIMIT || !(time >= 0 && time <= TIME_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid retro polygon request");

	mesh = &assets->meshes[instance < 2 ? 0 : 1];
	motion = assets->motion[instance];

	if (mesh->point_count > MH_RETRO_MAX_POINTS ||
		mesh->face_count > MH_RETRO_MAX_FACES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid retro mesh counts");

	for (axis = 0; axis < 3; axis++) {
		angles[axis] =
			(float)((long double)
					motion[MH_RETRO_ROTATION_RATE][axis] *
				time);
		bob[axis] =
			(float)((long double)motion[MH_RETRO_BOB_RATE][axis] *
				time);
		bob[axis] = (float)((long double)bob[axis] +
			motion[MH_RETRO_BOB_OFFSET][axis]);
	}

	screen_center[0] =
		(float)((long double)sin(bob[0]) * (double).4 + (double).5);
	screen_center[1] =
		(float)((long double)sin(bob[1]) * (double).2 + (double).35);
	status = mh_pose_matrix(angles, matrix, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(projected, 0, sizeof(projected));
	memset(polygons, 0, sizeof(polygons));

	for (point_index = 0; point_index < mesh->point_count; point_index++) {
		point = mesh->points[point_index];
		vertex = &projected[point_index];

		for (axis = 0; axis < 3; axis++)
			rotated[axis] =
				(float)(((long double)point[1] *
							matrix[axis + 3] +
						(long double)point[0] *
							matrix[axis]) +
					(long double)point[2] *
						matrix[axis + 6]);

		for (axis = 0; axis < 2; axis++)
			vertex->xy[axis] = (float)((long double)rotated[axis] *
					(double).1 * (width + height) *
					(double).5 +
				(long double)(axis ? height : width) *
					screen_center[axis]);

		vertex->depth =
			(float)((long double)rotated[2] * MESH_DEPTH_SCALE +
				MESH_DEPTH_CENTER);
		vertex->alpha = COLOR_BYTE_MAX;
	}

	visible_count = 0;

	for (face_index = 0; face_index < mesh->face_count; face_index++) {
		face = &mesh->faces[face_index];
		polygon = &polygons[visible_count];

		if ((face->count != 3 && face->count != 4) ||
			face->palette_index >= MH_RETRO_PALETTE_COUNT)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid retro face");

		polygon->count = face->count;

		for (corner = 0; corner < face->count; corner++) {
			if (face->indices[corner] >= mesh->point_count)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid retro face index");

			polygon->vertices[corner] =
				projected[face->indices[corner]];
		}

		area = ((long double)polygon->vertices[2].xy[1] -
			polygon->vertices[0].xy[1]) *
				((long double)polygon->vertices[1].xy[0] -
					polygon->vertices[0].xy[0]) -
			((long double)polygon->vertices[1].xy[1] -
				polygon->vertices[0].xy[1]) *
				((long double)polygon->vertices[2].xy[0] -
					polygon->vertices[0].xy[0]);

		if (!(area > 0))
			continue;

		normal_dot = ((long double)face->normal[1] * matrix[7] +
				(long double)face->normal[0] * matrix[6]) +
			(long double)face->normal[2] * matrix[8];
		shade = normal_dot * (double).45 + (double).55;

		for (channel = 0; channel < 3; channel++) {
			value = shade *
				assets->palette[face->palette_index][channel];

			if (!(value >= 0 && value < COLOR_BYTE_RANGE))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid retro shaded color");

			for (corner = 0; corner < face->count; corner++)
				polygon->vertices[corner].rgb[channel] =
					(float)(unsigned int)value;
		}

		visible_count++;
	}

	if (visible_count != 0)
		memcpy(output, polygons, visible_count * sizeof(*output));

	*count = visible_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
