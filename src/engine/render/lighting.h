#ifndef MH_RENDER_LIGHTING_H
#define MH_RENDER_LIGHTING_H

#include <stddef.h>

#include "engine/core/error.h"

enum mh_light_mode {
	MH_LIGHT_OBJECT = 2,
	MH_LIGHT_CAR = 4,
	/*
	 * Software unavailable objects: blue outline, without material
	 * lighting.
	 */
	MH_LIGHT_UNAVAILABLE = 8
};

struct mh_point_light {
	double	position[3];
	double	color[3];
	double	intensity; /* Signed: negative lights subtract illumination. */
	double	radius;
};

struct mh_directional_light {
	unsigned char	color[3];
	double		intensity;
	double		direction[3];
};

struct mh_prepared_light {
	double	direction[3];
	double	view[3];
	double	color[3];
	int	specular;
};

/*
 * Software frame setup: RGB * intensity * binary32 (1/255). Each multiplication
 * uses binary32 precision. Signed intensity is retained. Output is unchanged on
 * invalid/overflowing input, including an intermediate product outside the
 * float range.
 */
enum mh_error_code	mh_light_directional_color(const unsigned char rgb[3],
	float intensity, float color[3], struct mh_error *error_out);

/*
 * Object-origin attenuation. All positions and normals must use one orthonormal
 * coordinate frame (e.g. world space). No allocation or platform dependencies.
 * Output unchanged on failure. Coincident light/eye vectors become zero, not
 * stale runtime scratch.
 */
enum mh_error_code	mh_light_prepare(const struct mh_point_light *light,
	const double origin[3], const double eye[3], double intensity,
	struct mh_prepared_light *prepared, struct mh_error *error_out);

/*
 * Normal lighting, before material multiplication and clamping. Normals are
 * used as supplied, without normalization. This is one lighting mode, not the
 * full material dispatcher. Output unchanged on invalid input or overflow;
 * signed RGB is retained.
 */
enum mh_error_code	mh_light_shade(const struct mh_prepared_light *lights,
	size_t light_count, const double normal[3], const double ambient[3],
	double color[3], struct mh_error *error_out);

/*
 * Mode 2/4 normal shades with explicit binary32 stores. Mode 4 multiplies
 * highlights by four. Atomic output; prepared vectors share the normal's
 * orthonormal frame.
 */
enum mh_error_code	mh_light_shade_mode(
	const struct mh_prepared_light *lights, size_t light_count,
	const double normal[3], const double ambient[3], unsigned int mode,
	float color[3], struct mh_error *error_out);

/*
 * Unavailable-object material: first corner's RGB and linear camera depth / far
 * distance, shared by the whole triangle/quad. Light is the cached normal
 * shade. Returns byte-range RGB (5..70), with explicit binary32 stores. Atomic
 * on invalid/overflowing input.
 */
enum mh_error_code	mh_light_unavailable(const unsigned char rgb[3],
	float depth, const float light[3], float color[3],
	struct mh_error *error_out);

#endif
