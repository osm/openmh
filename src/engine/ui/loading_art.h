#ifndef MH_LOADING_ART_H
#define MH_LOADING_ART_H

#include "engine/core/error.h"
#include "interfaces/image/pixels.h"

#define MH_LOADING_WIDTH 640
#define MH_LOADING_HEIGHT 400
#define MH_LOADING_STAGES 6
#define MH_LOADING_COLUMNS 2
#define MH_LOADING_FRAMES 3
#define MH_LOADING_INDICATOR_WIDTH 12
#define MH_LOADING_INDICATOR_HEIGHT 11

struct mh_loading_indicator {
	unsigned int	x, y, width, height;
};

/* Validate decoded artwork dimensions without inspecting an encoded header. */
enum mh_error_code	mh_loading_background_check(
	const struct mh_image *image, struct mh_error *error_out);

/*
 * Prepare three top-down indicator frames, sharing frame zero's nonzero-index
 * coverage. Distinct indices remain distinct even with duplicate RGB entries.
 * The source is borrowed only during the call; output owns RGBA bytes freed by
 * the caller and is unchanged on failure. No encoding is assumed.
 */
enum mh_error_code	mh_loading_indicators_prepare(
	const struct mh_indexed_image *source, struct mh_image *output,
	struct mh_error *error_out);

/* Original bitmap indicator placement and rounded scaled sprite dimensions. */
enum mh_error_code	mh_loading_indicator_layout(unsigned int width,
	unsigned int height, unsigned int stage, unsigned int column,
	struct mh_loading_indicator *output, struct mh_error *error_out);

#endif
