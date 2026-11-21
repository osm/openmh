#ifndef MH_TYRE_EFFECTS_H
#define MH_TYRE_EFFECTS_H

#include "engine/core/error.h"
#include "interfaces/material/material.h"

struct mh_tyre_effects {
	unsigned long	id[2];

	/* Original binary32 values, retained when ID clears. */
	double		gain[2];
};

struct mh_tyre_effect_input {
	/* Rear right, rear left; unsupported material IDs are ignored. */
	unsigned long	material[2];
	int		supported[2], sliding, wheelspin, heavy_brake;
	double		forward, slide, rpm;
};

/* Clear IDs once before a frame's local physics slices; retain gains. */
void	mh_tyre_effects_begin_frame(struct mh_tyre_effects *effects);

/*
 * A slice without emission leaves prior frame-slice effects intact; caller
 * clears IDs with begin_frame before the first slice. Uses FINAL rear-wheel
 * materials and damped local Z. Slide needs Z > 3; wheelspin overrides slide,
 * heavy braking at Z > 3 overrides both. Materials >25 use entry zero.
 * ContactAnimID 1..8 all use the same gates; IDs >8 are ignored safely (the
 * original indexes past its table). Unknown emitter IDs 4..8 may be published,
 * but emit no particles. Output unchanged on error; state/output may alias.
 */
enum mh_error_code	mh_tyre_effects_update(
	const struct mh_materials *materials,
	const struct mh_tyre_effect_input *input,
	const struct mh_tyre_effects *state, struct mh_tyre_effects *output,
	struct mh_error *error_out);

#endif
