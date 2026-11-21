#ifndef MH_RENDER_GLENZ_H
#define MH_RENDER_GLENZ_H

#include "engine/core/error.h"

/* Per-object movement history; callers supply the initial position/phase. */
struct mh_glenz_scroll {
	float	previous[3];
	float	phase;
};

/*
 * PC 3.1 mode-4 environment-face RGB, before byte packing. light is the
 * prepared normal-light cache, not a direction.
 * Only non-emissive faces use this helper; output is clamped to [0,255].
 * This arithmetic alone does not select lighting modes or prepare lights.
 */
enum mh_error_code	mh_glenz_car_color(const unsigned char rgb[3],
	const float brightness[3], const float light[3], float color[3],
	struct mh_error *error_out);

/*
 * Call once at the car-shadow preparation point, before visibility culling,
 * when its renderer/shadow gates allow it. Uses rendered world position and the
 * object's forward basis vector. A successful update stores both phase and
 * previous position. Failure leaves history unchanged. Phase wraps once, not by
 * modulo arithmetic.
 */
enum mh_error_code	mh_glenz_scroll_update(struct mh_glenz_scroll *scroll,
	const float position[3], const float forward[3],
	struct mh_error *error_out);

/*
 * PC 3.1 reflection-map coordinates. Position, normal and eye use object
 * coordinates. Normals are not normalized here. Scroll is the object's
 * movement-driven phase, not elapsed time. Negative discriminants/roots produce
 * the (scroll, scroll) fallback. Invalid input leaves output unchanged. No
 * texture sampling.
 */
enum mh_error_code	mh_glenz_reflection_uv(const float position[3],
	const float normal[3], const float eye[3], float scroll, float uv[2],
	struct mh_error *error_out);

/*
 * PC 3.1 Phong-map coordinates. Normal has already been transformed to view
 * space. Screen/center are in pixels; the inverse horizontal half-width is
 * shared by both axes. Output is clamped to [0,1].
 */
enum mh_error_code	mh_glenz_phong_uv(const float normal[3],
	const float screen[2], const float center[2], float inverse_half_width,
	float uv[2], struct mh_error *error_out);

#endif
