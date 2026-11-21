#ifndef MH_MENU_THUMBNAIL_H
#define MH_MENU_THUMBNAIL_H

#include "engine/core/error.h"
#include "engine/ui/menu_canvas.h"
#include "interfaces/image/decoder.h"

struct mh_resource_source;

#define MH_MENU_THUMBNAIL_COLORS 64

struct mh_menu_thumbnail {
	/* 269 rows of 324 decoded palette indices. */
	unsigned char	*pixels;

	/* RGB indices 128..191. */
	unsigned char	palette[MH_MENU_THUMBNAIL_COLORS * 3];
};

/*
 * Prepare the first 269 rows of a decoded 324x269/270 indexed preview and copy
 * palette entries 128..191. Successful output owns pixels; free before reuse.
 * Failure preserves output. Source format interpretation belongs to its
 * decoder.
 */
enum mh_error_code	mh_menu_thumbnail_prepare(
	const struct mh_indexed_image *image, struct mh_menu_thumbnail *output,
	struct mh_error *error_out);
enum mh_error_code	mh_menu_thumbnail_load_from(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_menu_thumbnail *output,
	struct mh_error *error_out);
void			mh_menu_thumbnail_free(
	struct mh_menu_thumbnail *thumbnail);

/*
 * Oval crop. NULL thumbnail fills the same oval with 128. Clip writes to the
 * complete allocation, retaining offscreen padding.
 */
enum mh_error_code	mh_menu_thumbnail_draw(
	const struct mh_menu_thumbnail *thumbnail, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
