#ifndef MH_ENGINE_SOUND_H
#define MH_ENGINE_SOUND_H

#include "engine/core/error.h"
/* Native validation bound; the original formulas extrapolate beyond idle. */
#define MH_ENGINE_SOUND_RPM_LIMIT 1000000

enum mh_engine_sample {
	MH_SOUND_GAS_ON,
	MH_SOUND_GAS_RELEASE,
	MH_SOUND_GAS_ON_LEFT,
	MH_SOUND_GAS_ON_RIGHT,
	MH_SOUND_GAS_RELEASE_LEFT,
	MH_SOUND_GAS_RELEASE_RIGHT,
	MH_SOUND_INSIDE,
	MH_SOUND_TURBO,
	MH_ENGINE_SAMPLES
};

struct mh_sound_parameters {
	double	pitch, left, right;

	/* Original source values, before software ear clamps. */
	double	gain, pan;
};

/*
 * Controlled engine layer mix. blend is the published route sound blend; in_car
 * selects cockpit mixing. Accept extrapolated RPM in +/-1e6 without clamping
 * the formula. Device frequency limits apply later; output unchanged on
 * failure.
 */
enum mh_error_code	mh_engine_sound(double rpm, double pedal, double blend,
	int in_car, struct mh_sound_parameters output[MH_ENGINE_SAMPLES],
	struct mh_error *error_out);

#endif
