#ifndef MH_UI_RETRO_H
#define MH_UI_RETRO_H

#include "engine/core/error.h"
#include "engine/render/retro.h"
#include "engine/ui/hud.h"

#define MH_RETRO_MAX_VERTICES                                                  \
	((MH_RETRO_INSTANCE_COUNT * MH_RETRO_MAX_FACES +                       \
		 MH_RETRO_VISIBLE_LINES * (MH_RETRO_LINE_BYTES - 1) +          \
		 MH_RETRO_BACKDROP_QUADS) *                                    \
		MH_RETRO_QUAD_VERTICES)

struct mh_retro_draw_vertex {
	struct mh_retro_vertex	vertex;
	float			uv[2];
};

struct mh_retro_draw {
	struct mh_retro_draw_vertex	vertices[MH_RETRO_MAX_VERTICES];
	unsigned int			count, text_end, width, height;
	struct mh_retro_state		next;
};

/* Validate pass boundaries and finite vertex ranges before GPU submission. */
enum mh_error_code	mh_retro_draw_validate(const struct mh_retro_draw *draw,
	struct mh_error *error_out);

/*
 * Complete post-HUD geometry: 30 backdrop vertices, then atlas text through
 * text_end, then five mesh instances. Unit font scale, zero slant/shadow.
 * Consumes parsed resources. Failure leaves count zero; state is not advanced.
 * UVs address the shared HUD atlas, preserving original .49 texel insets
 * and the special projector's half-unit shift in 256-unit texture space.
 */
enum mh_error_code	mh_retro_draw(const struct mh_retro_assets *assets,
	const struct mh_hud_assets *hud, unsigned int width,
	unsigned int height, float dt, const struct mh_retro_state *state,
	struct mh_retro_draw *output, struct mh_error *error_out);

#endif
