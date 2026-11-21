#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/halo_internal.h"

#define VIEWPORT_LIMIT 20000
#define SCREEN_POSITION_LIMIT 1e30f
#define PROJECTION_LIMIT 1e6f
#define FLARE_LIMIT 1e9f
#define COLOR_CLASS_THRESHOLD 180
#define COLOR_BYTE_MAX 255
#define QUARTER_TURN 1.5707963705062866f
#define THREE_QUARTER_TURN 4.7123889923095703f
#define HALF_TURN 3.14159265359
#define FULL_TURN 6.28318530718
#define NEAR_DEPTH_CAP .9999899864196777f
#define GLOW_UV_RADIUS .17675000429153442

/* Shared depth-buffer profiles use these lens reflection colors. */
static const struct mh_halo_policy depth_policy = {0, 0, 1, 0, 0, 0,
	{{.6, .2, .6}, {.6, .6, .35}, {1, .75, .75}, {.45, .45, .85},
		{.85, .25, .25}, {.5, .5, .5}, {.15, .45, .15}}};

static int
bounded(float value, float limit)
{
	return value >= -limit && value <= limit;
}

/*
 * Return a binary32 angle in [0,2pi], including the negative-Y direction for a
 * coincident vector.
 */
static float
vector_angle(float x, float y)
{
	if (x == 0)
		return y > 0 ? QUARTER_TURN : THREE_QUARTER_TURN;

	return (float)(atan((double)y / x) +
		(x < 0			 ? HALF_TURN
				: y <= 0 ? FULL_TURN
					: 0));
}

static enum mh_error_code
validate_projection(
	const struct mh_halo_projection *projection, struct mh_error *error_out)
{
	unsigned int endpoint, axis;

	if (projection == NULL ||
		!(projection->width > 0 &&
			projection->width <= VIEWPORT_LIMIT) ||
		!(projection->height > 0 &&
			projection->height <= VIEWPORT_LIMIT) ||
		!(projection->pixel_aspect > 0 &&
			projection->pixel_aspect <= 100) ||
		!(projection->near_depth > 0 &&
			projection->near_depth < projection->far_depth &&
			projection->far_depth <= 1e6) ||
		!(projection->cue_start >= 0 && projection->cue_start < 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid halo projection");

	/*
	 * Pixel XY is perspective-divided, not a world position. A
	 * valid source beside the near plane can be millions of pixels off
	 * screen. Allow it through to the original midpoint cull, while
	 * retaining ample float headroom for differences, aspect and quad
	 * expansion.
	 */
	for (endpoint = 0; endpoint < 2; endpoint++)
		for (axis = 0; axis < 4; axis++)
			if (!bounded(projection->endpoint[endpoint][axis],
				axis < 2 ? SCREEN_POSITION_LIMIT
					: PROJECTION_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid halo endpoint");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Bias the midpoint reciprocal W in view space. */
static float
reciprocal_glow_depth(const struct mh_halo_projection *projection, double bias)
{
	float reciprocal;

	reciprocal = (float)(((long double)projection->endpoint[0][3] +
				projection->endpoint[1][3]) *
		.5L);

	return (float)(1 / (1 / (long double)reciprocal + bias));
}

static enum mh_error_code
world_halo_quads(const struct mh_halo_projection *projection,
	struct mh_effect_quad output[MH_WORLD_GLOW_QUADS],
	const struct mh_halo_policy *policy, struct mh_error *error_out)
{
	static const float colors[8][8] = {
		{60, 60, 60, 3.2f, 120, 120, 120, 2.65f},
		{27, 68, 255, 2.15f, 112, 255, 10, 2.04f},
		{60, 113, 184, 2.5f, 99, 255, 10, 3},
		{60, 113, 184, 2.5f, 99, 255, 10, 3},
		{255, 13, 0, 1.84f, 255, 96, 69, 3.19f},
		{150, 55, 0, 3.2f, 255, 71, 18, 2.65f},
		{111, 77, 25, 3.2f, 255, 125, 50, 4},
		{60, 60, 60, 3.2f, 120, 120, 120, 2.65f}};

	struct mh_effect_quad quads[MH_WORLD_GLOW_QUADS];
	float cx, cy, z, dx, dy, dz, length, size, half_width, half_height,
		fade, brightness, depth;
	float angle, ray, sine, cosine, u, v, uv_cosine, uv_sine;
	unsigned int i, j, mask;
	int visible;
	double distance_fade, view_depth, channel, swapped_uv;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing halo output");

	status = validate_projection(projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(quads, 0, sizeof(quads));
	cx = (float)(((double)projection->endpoint[0][0] +
			projection->endpoint[1][0]) *
		.5);
	cy = (float)(((double)projection->endpoint[0][1] +
			projection->endpoint[1][1]) *
		.5);
	z = (float)(((double)projection->endpoint[0][2] +
			projection->endpoint[1][2]) *
		.5);

	if (projection->endpoint[0][2] <= 0 ||
		projection->endpoint[1][2] <= 0 ||
		projection->endpoint[0][2] > 1 ||
		projection->endpoint[1][2] > 1 ||
		(policy->reciprocal_depth &&
			(projection->endpoint[0][2] == 1 ||
				projection->endpoint[1][2] == 1)) ||
		cx <= -projection->width * .5f ||
		cx >= projection->width * 1.5f ||
		cy <= -projection->height * .5f ||
		cy >= projection->height * 1.5f) {
		memcpy(output, quads, sizeof(quads));
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	dx = projection->endpoint[1][0] - projection->endpoint[0][0];
	dy = projection->endpoint[1][1] - projection->endpoint[0][1];
	dz = (projection->endpoint[1][2] - projection->endpoint[0][2]) *
		projection->far_depth;
	length = (float)sqrt(
		(double)dx * dx + (double)dy * dy + (double)dz * dz);
	distance_fade =
		((double)z * projection->far_depth -
			(float)(.8500000238418579 * projection->far_depth)) *
		(float)(1.0 /
			((double)projection->far_depth -
				(float)(projection->cue_start *
					projection->far_depth)));
	fade = (float)(1 - (distance_fade < 0 ? 0 : distance_fade));

	if (fade < 0)
		fade = 0;

	brightness = (float)((double)fade *
		((1 - (double)z) * (1 - (double)z) * 2.5 + .2));
	brightness *= .35f;
	size = (float)(projection->width * .05);
	size = (float)((1 - (double)z * .2) * size);
	half_width = (float)(((double)length * 1.3 + size) * 1.3);
	half_height = (float)(((double)length * .25 + size) *
		projection->pixel_aspect * 1.3);
	angle = vector_angle(dx, dy);
	ray = vector_angle(
		cx - projection->width * .5f, cy - projection->height);
	sine = (float)sin(angle);
	cosine = (float)cos(angle);
	view_depth = policy->reciprocal_depth
		? z
		: (float)((float)((double)z * projection->far_depth) - 8);
	depth = view_depth > projection->near_depth
		? (float)(projection->near_depth / (float)view_depth)
		: NEAR_DEPTH_CAP;

	mask = (projection->color[0] > COLOR_CLASS_THRESHOLD ? 4U : 0U) |
		(projection->color[1] > COLOR_CLASS_THRESHOLD ? 2U : 0U) |
		(projection->color[2] > COLOR_CLASS_THRESHOLD ? 1U : 0U);
	uv_cosine = (float)(cos((double)angle - ray) * GLOW_UV_RADIUS);
	uv_sine = (float)(sin((double)angle - ray) * GLOW_UV_RADIUS);

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 3; j++) {
			channel = (double)brightness * colors[mask][i * 4 + 3];
			channel *= colors[mask][i * 4 + j];
			quads[i].color[j] = (float)(channel > COLOR_BYTE_MAX
					? COLOR_BYTE_MAX
					: channel);
		}

		quads[i].depth = depth;

		if (policy->reciprocal_depth)
			quads[i].reciprocal_w = reciprocal_glow_depth(
				projection, policy->world_bias);

		for (j = 0; j < 4; j++) {
			u = j == 0 || j == 3 ? -1.0f : 1.0f;
			v = j < 2 ? -1.0f : 1.0f;
			quads[i].xy[j][0] = cx + u * half_width * cosine -
				v * half_height * sine;
			quads[i].xy[j][1] = cy + u * half_width * sine +
				v * half_height * cosine;
			quads[i].uv[j][0] = .25f + u * uv_cosine - v * uv_sine;
			quads[i].uv[j][1] = .25f + u * uv_sine + v * uv_cosine;

			if (i != 0) {
				swapped_uv = quads[i].uv[j][0];
				quads[i].uv[j][0] = quads[i].uv[j][1];
				quads[i].uv[j][1] = (float)swapped_uv;
			}
		}

		half_width = (float)(half_width * .3);
		half_height = (float)(half_height * .3);
	}

	visible = 0;

	for (i = 0; i < 4; i++)
		if (quads[0].xy[i][0] > 0 &&
			quads[0].xy[i][0] < projection->width &&
			quads[0].xy[i][1] > 0 &&
			quads[0].xy[i][1] < projection->height)
			visible = 1;

	if (!visible)
		memset(quads, 0, sizeof(quads));

	memcpy(output, quads, sizeof(quads));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
lens_quads(const struct mh_halo_projection *projection, float cx, float cy,
	float brightness, const struct mh_halo_policy *policy,
	struct mh_effect_quad quads[MH_LENS_QUADS])
{
	static const double positions[MH_LENS_QUADS] = {
		2, 1.45, .75, .25, -.5, -.8, -2};

	static const double sizes[MH_LENS_QUADS] = {5, 8, 9, 14, 8, 8, 5};

	float value, dx, dy, radius, radial, base, x, y, stored_width,
		stored_height;
	long double half_width, half_height;
	double distance, ray_x, ray_y;
	unsigned int i, j;

	value = (float)((long double)80 * brightness);

	if (value > COLOR_BYTE_MAX)
		value = COLOR_BYTE_MAX;

	if (value < 0)
		value = 0;

	dx = cx - projection->width * .5f;
	dy = cy - projection->height * .5f;
	distance = sqrt((double)dx * dx + (double)dy * dy);
	radius = (float)distance;
	radial = distance < projection->width * .5
		? (float)sin((double)(1 -
				(long double)radius *
					(float)(2 /
						(long double)projection
							->width)) *
			.5 * HALF_TURN)
		: 0;

	value = (float)((long double)value * radial);
	base = (float)((long double)projection->width * .001953125L *
		(double)1.7);
	ray_x = (double)cx - projection->width * .5;
	ray_y = (double)cy - projection->height * .5;

	if (radial > 0)
		for (i = 0; i < MH_LENS_QUADS; i++) {
			quads[i].depth = NEAR_DEPTH_CAP;
			quads[i].reciprocal_w = NEAR_DEPTH_CAP;
			x = (float)((long double)ray_x * positions[i] +
				projection->width * .5L);
			y = (float)((long double)ray_y * positions[i] +
				projection->height * .5L);
			half_width = (long double)base * sizes[i];
			half_height = half_width * projection->pixel_aspect;

			/*
			 * Sizes 5 and 8 are saved to floats and reused later.
			 */
			if (i != 2 && i != 3) {
				stored_width = (float)half_width;
				stored_height = (float)half_height;
				half_width = stored_width;
				half_height = stored_height;
			}

			for (j = 0; j < 3; j++)
				quads[i].color[j] = (float)((long double)value *
					policy->lens_colors[i][j]);

			for (j = 0; j < 4; j++) {
				quads[i].xy[j][0] = (float)(x +
					(j == 0 || j == 3 ? -half_width
							: half_width));
				quads[i].xy[j][1] = (float)(y +
					(j < 2 ? -half_height : half_height));

				/* 64px hwhex begins at atlas pixel (128,0). */
				quads[i].uv[j][0] =
					j == 0 || j == 3 ? .5f : .75f;
				quads[i].uv[j][1] = j < 2 ? 0 : .25f;
			}
		}
}

enum mh_error_code
mh_halo_world_lamp_quads(const struct mh_halo_projection *projection,
	int previous_visible, int flares, struct mh_depth_probe *probe,
	struct mh_effect_quad output[MH_WORLD_LAMP_QUADS],
	const struct mh_halo_policy *policy, struct mh_error *error_out)
{
	struct mh_effect_quad quads[MH_WORLD_LAMP_QUADS];
	struct mh_depth_probe next;
	enum mh_error_code status;
	float cx, cy, z, fade, brightness;
	double distance_fade, view_depth;

	if (probe == NULL || output == NULL ||
		(previous_visible != 0 && previous_visible != 1) ||
		(flares != 0 && flares != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid lamp state");

	memset(quads, 0, sizeof(quads));
	memset(&next, 0, sizeof(next));
	status = world_halo_quads(projection, quads, policy, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	cx = (float)(((long double)projection->endpoint[0][0] +
			projection->endpoint[1][0]) *
		.5L);
	cy = (float)(((long double)projection->endpoint[0][1] +
			projection->endpoint[1][1]) *
		.5L);
	z = (float)(((long double)projection->endpoint[0][2] +
			projection->endpoint[1][2]) *
		.5L);
	distance_fade = (float)((long double)z * projection->far_depth -
		(float)(.8500000238418579L * projection->far_depth));
	distance_fade = distance_fade < 0 ? 0
					: distance_fade *
			(float)(1 /
				((long double)projection->far_depth -
					(float)((long double)
							projection->cue_start *
						projection->far_depth)));
	fade = (float)(distance_fade > 1 ? 0 : 1 - distance_fade);

	if (!policy->reciprocal_depth) {
		if (cx <= 0 || cx >= projection->width || cy <= 0 ||
			cy >= projection->height || fade <= 0 ||
			projection->endpoint[0][2] <= 0 ||
			projection->endpoint[1][2] <= 0 ||
			projection->endpoint[0][2] >= 1 ||
			projection->endpoint[1][2] >= 1) {
			memset(quads, 0, sizeof(quads));
		} else {
			next.active = 1;
			next.x = cx;
			next.y = cy;
			view_depth = (float)((float)((long double)z *
						projection->far_depth) -
				8);
			next.depth = view_depth > projection->near_depth
				? (float)(projection->near_depth / view_depth)
				: NEAR_DEPTH_CAP;

			if (!previous_visible)
				memset(quads, 0, sizeof(quads));
		}
	}

	if (previous_visible && flares && quads[0].depth != 0) {
		/*
		 * World lamps use .65, independent of the .35 glow intensity
		 * and the lamp's color classification.
		 */
		brightness = (float)((long double)fade *
			((1 - (long double)z) * (1 - (long double)z) * 2.5L +
				(double).2));
		brightness = (float)((long double)brightness * .65f);
		lens_quads(projection, cx, cy, brightness, policy,
			quads + MH_WORLD_GLOW_QUADS);
	}

	*probe = next;
	memcpy(output, quads, sizeof(quads));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static float
halo_facing(const struct mh_halo_projection *projection,
	const struct mh_attached_halo *style, float cx, float cy, float z)
{
	float ray[3], factor, facing;
	long double distance;
	unsigned int axis;

	/*
	 * Reconstruct the midpoint ray, normalize, and negate the dot product
	 * with the transformed source direction.
	 */
	ray[2] = (float)((long double)z * projection->far_depth);
	factor = (float)((long double)z * projection->far_depth *
		style->inverse_focal / (projection->width * .5f));
	ray[0] = (float)(((long double)cx - projection->width * .5f) * factor);
	ray[1] = (float)(((long double)projection->height * .5f - cy) * factor *
		(float)(1 / (long double)projection->pixel_aspect));
	distance = sqrt((double)(((long double)ray[1] * ray[1] +
					(long double)ray[0] * ray[0]) +
		(long double)ray[2] * ray[2]));

	if (distance == 0)
		return 0;

	for (axis = 0; axis < 3; axis++)
		ray[axis] = (float)(ray[axis] / distance);

	facing = (float)(-(((long double)style->direction[1] * ray[1] +
				(long double)style->direction[0] * ray[0]) +
		(long double)style->direction[2] * ray[2]));

	return facing;
}

enum mh_error_code
mh_halo_attached_quads(const struct mh_halo_projection *projection,
	const struct mh_attached_halo *style, int previous_visible, int flares,
	struct mh_depth_probe *probe,
	struct mh_effect_quad output[MH_ATTACHED_LAMP_QUADS],
	const struct mh_halo_policy *policy, struct mh_error *error_out)
{
	static const float colors[8][4] = {{60, 60, 85, 3.2f},
		{27, 68, 255, 2.15f}, {60, 113, 184, 2.5f},
		{60, 113, 184, 2.5f}, {255, 13, 0, 1.84f}, {150, 55, 0, 3.2f},
		{111, 77, 25, 3.2f}, {60, 60, 60, 3.2f}};

	struct mh_effect_quad quads[MH_ATTACHED_LAMP_QUADS];
	struct mh_depth_probe next;
	enum mh_error_code status;
	float cx, cy, z, facing, factor, dx, dy, dz, length;
	float fade, brightness, glow, size, half_width, half_height, angle, ray,
		sine, cosine, uv_cosine, uv_sine, u, v;
	long double value, curve;
	double distance_fade, view_depth;
	unsigned int i, j, mask;
	int visible;

	if (style == NULL || probe == NULL || output == NULL ||
		(previous_visible != 0 && previous_visible != 1) ||
		(flares != 0 && flares != 1) ||
		(style->flags != MH_HALO_GLOW &&
			style->flags != MH_HALO_GLOW_FLARES &&
			style->flags != MH_HALO_HEADLIGHT &&
			style->flags != MH_HALO_TAILLIGHT) ||
		!(style->intensity >= 0 && style->intensity <= 100) ||
		!(style->size >= 0 && style->size <= 100) ||
		!bounded(style->flare, FLARE_LIMIT) ||
		!(style->inverse_focal > 0 && style->inverse_focal <= 100) ||
		(style->scenery != 0 && style->scenery != 1) ||
		(style->tinted != 0 && style->tinted != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid attached halo state");

	for (i = 0; i < 3; i++)
		if (!bounded(style->direction[i], 2) ||
			!bounded(style->tint[i], 100))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid attached halo direction/tint");

	status = validate_projection(projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(quads, 0, sizeof(quads));
	memset(&next, 0, sizeof(next));
	cx = (float)(((long double)projection->endpoint[0][0] +
			projection->endpoint[1][0]) *
		.5L);
	cy = (float)(((long double)projection->endpoint[0][1] +
			projection->endpoint[1][1]) *
		.5L);
	z = (float)(((long double)projection->endpoint[0][2] +
			projection->endpoint[1][2]) *
		.5L);

	if (cx <= projection->width * policy->clip_min ||
		cx >= projection->width * policy->clip_max ||
		cy <= projection->height * policy->clip_min ||
		cy >= projection->height * policy->clip_max ||
		projection->endpoint[0][2] <= 0 ||
		projection->endpoint[1][2] <= 0 ||
		projection->endpoint[0][2] >= 1 ||
		projection->endpoint[1][2] >= 1)
		goto done;

	facing = 1;

	if (style->flags & MH_HALO_DIRECTIONAL) {
		facing = halo_facing(projection, style, cx, cy, z);

		if (facing <= 0)
			goto done;
	}

	distance_fade = (float)((long double)z * projection->far_depth -
		(float)(.8500000238418579L * projection->far_depth));
	distance_fade = distance_fade < 0 ? 0
					: distance_fade *
			(float)(1 /
				((long double)projection->far_depth -
					(float)((long double)
							projection->cue_start *
						projection->far_depth)));
	fade = (float)((distance_fade > 1 ? 0 : 1 - distance_fade) *
		(long double)facing);

	if (fade <= 0)
		goto done;

	value = 1 - (long double)z;
	curve = style->flags & MH_HALO_REAR
		? value * value * value * value * (double)1.4 + (double).4
		: value * value * value * 2.5L +
			(style->flags & MH_HALO_DIRECTIONAL ? (double).35
							: (double).2);
	brightness = (float)(curve * fade);
	glow = (float)((long double)brightness * style->intensity);
	mask = (projection->color[0] > COLOR_CLASS_THRESHOLD ? 4U : 0U) |
		(projection->color[1] > COLOR_CLASS_THRESHOLD ? 2U : 0U) |
		(projection->color[2] > COLOR_CLASS_THRESHOLD ? 1U : 0U);

	for (i = 0; i < 3; i++) {
		quads[0].color[i] = (float)((long double)glow *
			colors[mask][3] * colors[mask][i]);

		if (style->scenery && style->tinted)
			quads[0].color[i] =
				(float)((long double)quads[0].color[i] *
					style->tint[i]);

		if (quads[0].color[i] < 0)
			quads[0].color[i] = 0;

		if (quads[0].color[i] > COLOR_BYTE_MAX)
			quads[0].color[i] = COLOR_BYTE_MAX;
	}

	dx = (float)((long double)projection->endpoint[1][0] -
		projection->endpoint[0][0]);
	dy = (float)((long double)projection->endpoint[1][1] -
		projection->endpoint[0][1]);
	dz = (float)(((long double)projection->endpoint[1][2] -
			projection->endpoint[0][2]) *
		projection->far_depth);
	length = (float)sqrt((double)((long double)dy * dy +
		(long double)dx * dx + (long double)dz * dz));
	size = (float)((long double)style->size * projection->width *
		(double).05);
	factor = (float)(1 - (long double)z);

	if (style->flags & MH_HALO_DIRECTIONAL)
		size = (float)((long double)size * factor *
			(style->flags & MH_HALO_REAR ? 1
						: (long double)factor));
	else
		size = (float)((1 - (long double)z * (double).2) * size);
	half_width = (float)((long double)length * (double)1.3 + size);
	half_height = (float)(((long double)length * .25L + size) *
		projection->pixel_aspect);
	view_depth = style->flags & (MH_HALO_DIRECTIONAL | MH_HALO_REAR)
		? (float)((long double)z * projection->far_depth + (double)-2.2)
		: (float)((float)((long double)z * projection->far_depth) +
			(long double)-3.5);

	if (policy->reciprocal_depth)
		view_depth = z;

	next.active = 1;
	next.x = cx;
	next.y = cy;
	next.depth = view_depth > projection->near_depth
		? (float)(projection->near_depth / (long double)view_depth)
		: NEAR_DEPTH_CAP;

	if (!previous_visible) {
		memset(quads, 0, sizeof(quads));
		goto done;
	}

	quads[0].depth = next.depth;

	if (policy->reciprocal_depth)
		quads[0].reciprocal_w = reciprocal_glow_depth(projection,
			style->flags & (MH_HALO_DIRECTIONAL | MH_HALO_REAR)
				? policy->directional_bias
				: policy->attached_bias);

	angle = vector_angle(dx, dy);
	ray = vector_angle(
		cx - projection->width * .5f, cy - projection->height);
	sine = (float)sin(angle);
	cosine = (float)cos(angle);
	uv_cosine = (float)(cos((double)angle - ray) * GLOW_UV_RADIUS);
	uv_sine = (float)(sin((double)angle - ray) * GLOW_UV_RADIUS);
	visible = 0;

	for (j = 0; j < 4; j++) {
		u = j == 0 || j == 3 ? -1.0f : 1.0f;
		v = j < 2 ? -1.0f : 1.0f;
		quads[0].xy[j][0] =
			(float)(cx + (long double)u * half_width * cosine -
				(long double)v * half_height * sine);
		quads[0].xy[j][1] =
			(float)(cy + (long double)u * half_width * sine +
				(long double)v * half_height * cosine);
		quads[0].uv[j][0] = .25f + u * uv_cosine - v * uv_sine;
		quads[0].uv[j][1] = .25f + u * uv_sine + v * uv_cosine;

		if (quads[0].xy[j][0] > 0 &&
			quads[0].xy[j][0] < projection->width &&
			quads[0].xy[j][1] > 0 &&
			quads[0].xy[j][1] < projection->height)
			visible = 1;
	}

	if (!visible) {
		memset(quads, 0, sizeof(quads));
		goto done;
	}

	if (style->scenery || ((style->flags & MH_HALO_FLARES) && flares)) {
		brightness = (float)((long double)brightness * style->flare);

		if (style->scenery) {
			factor = 0;

			if (style->tinted)
				for (i = 0; i < 3; i++)
					if (style->tint[i] > factor)
						factor = style->tint[i];

			brightness = (float)((long double)brightness * factor);
		}

		lens_quads(projection, cx, cy, brightness, policy, quads + 1);
	}

done:
	*probe = next;
	memcpy(output, quads, sizeof(quads));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/*
 * Keep the original Glide/Direct3D depth/history contract at existing call
 * sites.
 */
enum mh_error_code
mh_world_halo_quads(const struct mh_halo_projection *projection,
	struct mh_effect_quad output[MH_WORLD_GLOW_QUADS],
	struct mh_error *error_out)
{
	return world_halo_quads(projection, output, &depth_policy, error_out);
}

enum mh_error_code
mh_attached_halo_quads(const struct mh_halo_projection *projection,
	const struct mh_attached_halo *style, int previous_visible, int flares,
	struct mh_depth_probe *probe,
	struct mh_effect_quad output[MH_ATTACHED_LAMP_QUADS],
	struct mh_error *error_out)
{
	return mh_halo_attached_quads(projection, style, previous_visible,
		flares, probe, output, &depth_policy, error_out);
}

enum mh_error_code
mh_world_lamp_quads(const struct mh_halo_projection *projection,
	int previous_visible, int flares, struct mh_depth_probe *probe,
	struct mh_effect_quad output[MH_WORLD_LAMP_QUADS],
	struct mh_error *error_out)
{
	return mh_halo_world_lamp_quads(projection, previous_visible, flares,
		probe, output, &depth_policy, error_out);
}
