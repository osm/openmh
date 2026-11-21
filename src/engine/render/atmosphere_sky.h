#ifndef MH_RENDER_SKY_H
#define MH_RENDER_SKY_H

#include "engine/core/error.h"
#include "engine/render/hidden_weather.h"
#include "engine/render/mesh.h"
#include "engine/render/sky.h"
#include "interfaces/catalog/track.h"

struct mh_image_decoder_provider;

/*
 * Decoder and bytes are borrowed during creation. Decode the panorama, resize
 * to the patched hardware width (256) with centered nearest samples and retain
 * its height. Owns the dynamic mesh, resized RGBA pixels and a white fill
 * texture. Output unchanged on failure.
 */
enum mh_error_code	mh_sky_mesh_create(
	const struct mh_image_decoder_provider *image_decoder,
	const unsigned char *data, size_t size, struct mh_mesh *output,
	struct mh_error *error_out);
enum mh_error_code	mh_sky_mesh_update(struct mh_mesh *mesh,
	struct mh_sky_state *state, const struct mh_track *track,
	const float world_to_clip[16], const float world_to_view[16],
	unsigned int width, unsigned int height, struct mh_error *error_out);

/*
 * Fresh-session SkyImage Off: camera-rotated top/bottom colors, no panorama
 * draw. Uses the existing mesh allocation; normal update restores the image.
 */
enum mh_error_code	mh_sky_mesh_plain(struct mh_mesh *mesh,
	struct mh_sky_state *state, const struct mh_track *track,
	const float world_to_clip[16], const float world_to_view[16],
	unsigned int width, unsigned int height, struct mh_error *error_out);

/*
 * Original hidden overhead modes draw a solid black background at far
 * depth. Leaves animation state alone; normal update restores all draws.
 */
enum mh_error_code	mh_sky_mesh_black(struct mh_mesh *mesh,
	struct mh_error *error_out);

/*
 * Install the track environment image with mh_mesh_set_environment before
 * GPU upload. These quads preserve original reciprocal W and per-corner RGB.
 */
enum mh_error_code	mh_sky_mesh_weather(struct mh_mesh *mesh,
	const struct mh_weather_camera *camera, struct mh_error *error_out);

#endif
