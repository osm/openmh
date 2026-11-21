#ifndef MH_TRON_INTRO_H
#define MH_TRON_INTRO_H

#include "engine/core/error.h"

#define MH_TRON_INTRO_LINES 15
#define MH_TRON_INTRO_VISIBLE_LINES 5

struct mh_tron_intro {
	float		next_time;
	int		backdrop;

	/* Pixel Y and alpha in 0..255. */
	float		band_top, band_bottom, opacity;
	unsigned int	count;

	/* Logical 512x384 space. */
	struct {
		unsigned int	line;
		int		x, y;
	} lines[MH_TRON_INTRO_VISIBLE_LINES];
};

/*
 * Sample before advancing the binary32 presentation timer. Renderer setup
 * resets time to zero; inactive rendering leaves it unchanged.
 */
enum mh_error_code	mh_tron_intro_frame(unsigned int width,
	unsigned int height, float time, float dt, int active,
	struct mh_tron_intro *output, struct mh_error *error_out);
const char	       *mh_tron_intro_text(unsigned int line);

/*
 * Independently project a logical glyph corner and its vertical fade. No
 * texture or font state is changed; errors preserve all outputs.
 */
enum mh_error_code	mh_tron_intro_corner(unsigned int width,
	unsigned int height, float x, float y, float xy[2], float *alpha,
	struct mh_error *error_out);

#endif
