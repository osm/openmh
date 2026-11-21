#ifndef MH_MENU_BACKDROP_H
#define MH_MENU_BACKDROP_H

#include "engine/core/error.h"
#include "engine/ui/menu_canvas.h"

#define MH_MENU_BACKDROP_COLORS 64

struct mh_indexed_image;
struct mh_image_decoder_provider;
struct mh_resource_source;

struct mh_menu_backdrop {
	/* Owned 960x640 indexed allocation. */
	unsigned char	*pixels;

	/* 64 RGB triples for indices 192..255. */
	unsigned char	palette[MH_MENU_BACKDROP_COLORS * 3];
};

/*
 * The menus' fixed 640x400, 63-color indexed image policy. The source is
 * top-down and occupies padded rows 100..499 at X=160. The decoded source is
 * borrowed for the call; output owns its pixels. Unlike a generic RGBA decode,
 * retain palette indices for additive effects. Failure is atomic; free a
 * successful result before reusing its output.
 */
enum mh_error_code	mh_menu_backdrop_prepare(
	const struct mh_indexed_image *image, struct mh_menu_backdrop *output,
	struct mh_error *error_out);

/*
 * Decode through the selected provider, then apply the palette/layout policy.
 */
enum mh_error_code	mh_menu_backdrop_load_from(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_menu_backdrop *output, struct mh_error *error_out);
void			mh_menu_backdrop_free(
	struct mh_menu_backdrop *backdrop);

/*
 * Present the previous work frame, then restore the centered backdrop into work
 * for the next drawing pass. Width is a positive multiple of 8 <=960; height is
 * 1..600. Each canvas viewport starts at begin; end is an exclusive allocation
 * clip bound. Backdrop/work/present storage must be disjoint. Padding is
 * preserved; errors leave both canvases untouched.
 */
enum mh_error_code	mh_menu_backdrop_exchange(
	const struct mh_menu_backdrop *backdrop, unsigned int width,
	unsigned int height, struct mh_menu_canvas *work,
	struct mh_menu_canvas *present, struct mh_error *error_out);

#endif
