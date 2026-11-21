#include <math.h>
#include <string.h>

#include "backends/render/software_halo_geometry.h"

#define VALUE_LIMIT 1e6f
#define TEXTURE_EXTENT_MAX 256
#define COLOR_BYTE_MAX 255
#define ORIGINAL_HALF_PI 1.57079637f
#define ORIGINAL_THREE_HALF_PI 4.71238899f
#define ORIGINAL_PI 3.14159265359
#define GLOW_SCREEN_SIZE 0.05
#define GLOW_LENGTH 1.5
#define GLOW_THICKNESS 0.15
#define GLOW_UV_RADIUS 0.707106
#define LENS_SCREEN_SIZE 0.001953125

/* Seven-quad lens chain. */
static const struct {
	double	position, size, color[3];
} lenses[] = {{2, 5, {.6, .2, .6}}, {1.45, 8, {.6, .6, .35}},
	{.75, 9, {1, .75, .75}}, {.25, 14, {.45, .45, .85}},
	{-.5, 8, {.85, .25, .25}}, {-.8, 8, {.5, .5, .5}},
	{-2, 5, {.15, .45, .15}}};

static int
bounded(float value)
{
	return value >= -VALUE_LIMIT && value <= VALUE_LIMIT;
}

/* Use atan(y/x) and explicit quadrants, including the zero vector. */
static float
vector_angle(float x, float y)
{
	double value;

	if (x == 0)
		return y > 0 ? ORIGINAL_HALF_PI : ORIGINAL_THREE_HALF_PI;

	value = (float)((double)y / x);
	value = atan(value);

	if (x < 0)
		value = (float)((long double)value + ORIGINAL_PI);
	else if (y <= 0)
		value = (float)((long double)value + 2 * ORIGINAL_PI);

	return (float)value;
}

static float
vector_length(const float vector[3])
{
	float x, y, z, sum;

	x = (float)((double)vector[0] * vector[0]);
	y = (float)((double)vector[1] * vector[1]);
	z = (float)((double)vector[2] * vector[2]);
	sum = (float)((double)y + x);
	sum = (float)((double)sum + z);

	return (float)sqrt(sum);
}

static float
facing(const struct mh_software_halo_source *source,
	const struct mh_projection *projection, const float center[3])
{
	float ray[3], factor, inverse, products[3], sum;
	unsigned int axis;

	if (!(source->flags & MH_SOFTWARE_HALO_DIRECTIONAL))
		return 1;

	factor = (float)((double)center[2] * projection->inverse_focal);
	factor = (float)((double)factor / projection->clip.half_width);
	ray[0] = (float)((double)center[0] - projection->clip.center[0]);
	ray[0] = (float)((double)ray[0] * factor);

	/* Use full viewport height, not center Y. */
	ray[1] = (float)((double)projection->clip.extent[1] - center[1]);
	ray[1] = (float)((double)ray[1] * factor);
	ray[1] = (float)((double)ray[1] * projection->inverse_aspect);
	ray[2] = center[2];
	inverse = (float)(1.0 / vector_length(ray));

	for (axis = 0; axis < 3; axis++) {
		ray[axis] = (float)((double)ray[axis] * inverse);
		products[axis] =
			(float)((double)source->direction[axis] * ray[axis]);
	}

	sum = (float)((double)products[0] + products[2]);
	sum = (float)((double)sum + products[1]);

	return -sum;
}

static float
brightness(const struct mh_software_halo_source *source,
	const struct mh_projection *projection,
	const struct mh_software_fog *fog, const float center[3])
{
	float fade, factor, depth, value;
	unsigned int i, power;

	factor = facing(source, projection, center);

	if (factor <= 0)
		return 0;

	fade = (float)((double)center[2] - fog->start_distance);
	fade = fade < 0 ? 0 : (float)((double)fade * fog->inverse_range);
	fade = fade > 1 ? 0 : (float)(1.0 - fade);
	fade = (float)((double)fade * factor);
	depth = (float)((double)center[2] * projection->inverse_far);
	depth = (float)(1.0 - depth);

	if (source->flags & MH_SOFTWARE_HALO_WORLD)
		value = (float)((long double)depth + .75);
	else {
		power = source->flags & MH_SOFTWARE_HALO_REAR ? 4 : 3;
		value = (float)((long double)depth * (power == 4 ? 1.4 : 2.5));

		for (i = 1; i < power; i++)
			value = (float)((double)value * depth);

		value = (float)((long double)value + .2);
	}

	return (float)((double)value * fade);
}

static void
shade_color(const struct mh_software_halo_source *source, float light,
	float gain, int clamp, float output[3])
{
	float factor, value;
	unsigned int channel;

	factor = (float)((double)light * gain);

	for (channel = 0; channel < 3; channel++) {
		value = (float)((double)factor * source->color[channel]);

		if (source->tinted)
			value = (float)((double)value * source->tint[channel]);

		if (value < 0)
			value = 0;

		if (clamp && value > COLOR_BYTE_MAX)
			value = COLOR_BYTE_MAX;

		output[channel] = value;
	}
}

/* Rotated rectangle: each multiply/add follows scene binary32 arithmetic. */
static void
rotated_quad(float center_x, float center_y, float width, float height,
	double sine, double cosine, float output[4][2])
{
	float x, y, cx, sy, sx, cy;
	unsigned int corner;

	for (corner = 0; corner < 4; corner++) {
		x = corner == 0 || corner == 3 ? -width : width;
		y = corner < 2 ? -height : height;
		cx = (float)((long double)cosine * x);
		sy = (float)((long double)sine * y);
		sx = (float)((long double)sine * x);
		cy = (float)((long double)cosine * y);
		x = (float)((double)cx - sy);
		y = (float)((double)sx + cy);
		output[corner][0] = (float)((double)center_x + x);
		output[corner][1] = (float)((double)center_y + y);
	}
}

static void
set_additive_quad(struct mh_software_clipped_face *face, float xy[4][2],
	float uv[4][2], const float rgb[3])
{
	struct mh_software_clipped_corner *vertex;
	unsigned int corner;

	face->flags =
		MH_NEAR_QUAD | MH_NEAR_TEXTURED | MH_SOFTWARE_FACE_ADDITIVE;
	face->count = 4;

	for (corner = 0; corner < 4; corner++) {
		vertex = &face->corners[corner];
		memcpy(vertex->xy, xy[corner], sizeof(vertex->xy));
		memcpy(vertex->uv, uv[corner], sizeof(vertex->uv));
		memcpy(vertex->rgb, rgb, sizeof(vertex->rgb));
	}
}

static void
glow_face(const struct mh_software_halo_source *source,
	const struct mh_projection *projection,
	const struct mh_indexed_image *image, const float center[3],
	float light, struct mh_software_clipped_face *face)
{
	float delta[3], extent, base, width, height, rotation, radial, radius;
	float xy[4][2], uv[4][2], rgb[3];
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		delta[axis] = (float)((double)source->endpoints[1].value[axis] -
			source->endpoints[0].value[axis]);

	extent = vector_length(delta);
	base = (float)((double)source->size * projection->clip.extent[0]);
	base = (float)((long double)base * GLOW_SCREEN_SIZE);
	width = (float)((long double)extent * GLOW_LENGTH);
	width = (float)((double)width + base);
	height = (float)((long double)extent * GLOW_THICKNESS);
	height = (float)((double)height + base);
	height = (float)((double)height * projection->clip.aspect);
	rotation = vector_angle(delta[0], delta[1]);
	rotated_quad(center[0], center[1], width, height, (float)sin(rotation),
		cos(rotation), xy);

	/* Glow UV orientation also uses full viewport height. */
	radial = vector_angle(
		(float)((double)center[0] - projection->clip.center[0]),
		(float)((double)center[1] - projection->clip.extent[1]));
	rotation = (float)((double)rotation - radial);
	radius = image->width * .5f;
	radius = (float)((long double)radius * GLOW_UV_RADIUS);
	radius = (float)((long double)radius - .5);
	rotated_quad(image->width * .5f, image->height * .5f, radius, radius,
		sin(rotation), cos(rotation), uv);
	shade_color(source, light, source->intensity, 0, rgb);
	set_additive_quad(face, xy, uv, rgb);
}

static void
lens_faces(const struct mh_software_halo_source *source,
	const struct mh_projection *projection,
	const struct mh_indexed_image *image, const float center[3],
	float light, struct mh_software_halo_geometry *output)
{
	float delta[3], radial, factor, base, x, y, width, height;
	float rgb[3], shade[3], xy[4][2], uv[4][2];
	unsigned int i, channel;

	delta[0] = (float)((double)center[0] - projection->clip.center[0]);
	delta[1] = (float)((double)center[1] - projection->clip.center[1]);
	delta[2] = 0;
	radial = vector_length(delta);

	if (radial >= projection->clip.center[0])
		return;

	factor = (float)((double)radial * projection->inverse_half_width);
	factor = (float)(1.0 - factor);
	factor = (float)((long double)factor * .5);
	factor = (float)((long double)factor * ORIGINAL_PI);
	factor = (float)sin(factor);

	if (factor <= 0)
		return;

	shade_color(source, light, source->flare, 1, rgb);

	for (channel = 0; channel < 3; channel++)
		rgb[channel] = (float)((double)rgb[channel] * factor);

	for (i = 0; i < 4; i++) {
		uv[i][0] = i == 0 || i == 3 ? .5f : image->width - .5f;
		uv[i][1] = i < 2 ? .5f : image->height - .5f;
	}

	base = (float)((long double)projection->clip.extent[0] *
		LENS_SCREEN_SIZE);

	for (i = 0; i < sizeof(lenses) / sizeof(lenses[0]); i++) {
		x = (float)((long double)delta[0] * lenses[i].position);
		y = (float)((long double)delta[1] * lenses[i].position);
		x = (float)((double)x + projection->clip.center[0]);
		y = (float)((double)y + projection->clip.center[1]);
		width = (float)((long double)base * lenses[i].size);
		height = (float)((double)width * projection->clip.aspect);
		rotated_quad(x, y, width, height, 0, 1, xy);

		for (channel = 0; channel < 3; channel++)
			shade[channel] = (float)((long double)rgb[channel] *
				lenses[i].color[channel]);

		set_additive_quad(&output->faces[i + 1], xy, uv, shade);
		output->images[i + 1] = image;
		output->count++;
	}
}

static int
image_valid(const struct mh_indexed_image *image)
{
	return image != NULL && image->width > 0 &&
		image->width <= TEXTURE_EXTENT_MAX && image->height > 0 &&
		image->height <= TEXTURE_EXTENT_MAX;
}

enum mh_error_code
mh_software_halo_geometry(const struct mh_software_halo_source *source,
	const struct mh_projection *projection,
	const struct mh_software_fog *fog,
	const struct mh_software_depth_band *band,
	const struct mh_indexed_image *glow,
	const struct mh_indexed_image *lens, int lenses_enabled,
	struct mh_software_halo_geometry *output, struct mh_error *error_out)
{
	struct mh_software_halo_geometry next;
	float center[3], light, bucket, inverse_range;
	unsigned int i, j;
	enum mh_error_code status;

	if (source == NULL || fog == NULL || band == NULL || output == NULL ||
		!image_valid(glow) || (lenses_enabled && !image_valid(lens)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software halo geometry inputs");

	status = mh_software_projection_check(projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!bounded(source->intensity) || !bounded(source->size) ||
		!bounded(source->flare) || !bounded(fog->start_distance) ||
		!(fog->inverse_range > 0 &&
			fog->inverse_range <= VALUE_LIMIT) ||
		!(band->offset >= 0 && band->scale >= 0 &&
			band->offset + band->scale <= 1.000001f))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software halo factors");

	for (i = 0; i < 3; i++)
		if (!bounded(source->direction[i]) ||
			(source->tinted && !bounded(source->tint[i])))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software halo direction or tint");

	memset(&next, 0, sizeof(next));

	if (!(source->flags & MH_SOFTWARE_HALO_ENABLED) ||
		((source->endpoints[0].outcode | source->endpoints[1].outcode) &
			(MH_CLIP_NEAR | MH_CLIP_FAR)) ||
		(source->endpoints[0].outcode & source->endpoints[1].outcode)) {
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 2; j++)
			if (!bounded(source->endpoints[j].value[i]))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"invalid software halo endpoint");

		center[i] = (float)((double)source->endpoints[0].value[i] +
			source->endpoints[1].value[i]);
		center[i] = (float)((double)center[i] * .5);
	}

	if (center[0] <= 0 || center[0] >= projection->clip.extent[0] ||
		center[1] <= 0 || center[1] >= projection->clip.extent[1]) {
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (!(center[2] > projection->clip.near_distance &&
		center[2] < projection->far_distance))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"invalid software halo depth");

	light = brightness(source, projection, fog, center);

	if (light <= 0) {
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	inverse_range = (float)(1.0L /
		((long double)projection->far_distance -
			projection->clip.near_distance));
	bucket = (float)((double)center[2] - projection->clip.near_distance);
	bucket = (float)((double)bucket * inverse_range);
	bucket = (float)(1.0 - bucket);
	bucket = (float)((double)bucket * band->scale);
	bucket = (float)((double)bucket + band->offset);
	bucket = (float)((double)bucket * (MH_SOFTWARE_BUCKETS - 1));

	if (!(bucket >= 0 && bucket < MH_SOFTWARE_BUCKETS))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software halo outside ordering buckets");

	next.bucket = (unsigned int)bucket;
	memcpy(next.marker, center, sizeof(next.marker));
	next.count = 1;
	next.images[0] = glow;
	glow_face(source, projection, glow, center, light, &next.faces[0]);

	if (lenses_enabled && (source->flags & MH_SOFTWARE_HALO_LENSES))
		lens_faces(source, projection, lens, center, light, &next);

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
