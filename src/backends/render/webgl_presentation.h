#ifndef MH_WEBGL_PRESENTATION_H
#define MH_WEBGL_PRESENTATION_H

#include "backends/render/webgl_target.h"

/* Persistent download storage. Poll never waits for GPU completion. */
struct mh_webgl_download {
	GLuint		buffer;
	GLsync		fence;
	struct mh_image	image;
};

/*
 * Scale the full color target to the current canvas, matching native blitting.
 */
enum mh_error_code	mh_webgl_present(const struct mh_webgl_target *source,
	unsigned int width, unsigned int height, struct mh_error *error_out);

/* Owned, top-down RGBA8 pixels; failed read leaves output unchanged. */
enum mh_error_code	mh_webgl_read_pixels(
	const struct mh_webgl_target *source, struct mh_image *output,
	struct mh_error *error_out);

enum mh_error_code	mh_webgl_download_queue(
	struct mh_webgl_download *download,
	const struct mh_webgl_target *source, struct mh_error *error_out);
enum mh_error_code	mh_webgl_download_read(
	struct mh_webgl_download *download, int wait, int *ready,
	struct mh_error *error_out);
void			mh_webgl_download_destroy(
	struct mh_webgl_download *download);

#endif
