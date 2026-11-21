#ifndef MH_INPUT_FORCE_H
#define MH_INPUT_FORCE_H

#include "engine/core/error.h"

struct mh_input;
struct mh_input_provider;

#define MH_FORCE_SCALE 10000
#define MH_FORCE_INFINITE 0xffffffffUL

/* One independently supported slot per effect type; no implicit rumble. */
enum mh_force_kind {
	MH_FORCE_CONSTANT,
	MH_FORCE_TRIANGLE,
	MH_FORCE_SPRING,
	MH_FORCE_EFFECTS
};

/*
 * Magnitude/coefficients use signed +/-MH_FORCE_SCALE; saturation/deadband
 * use 0..MH_FORCE_SCALE. Direction is a Cartesian vector on the joystick's
 * first two axes. Period applies to triangle; condition arrays to spring.
 * Durations are milliseconds (or MH_FORCE_INFINITE), periods microseconds.
 */
struct mh_force_effect {
	unsigned long	duration_ms, period_us;
	long		direction[2], magnitude;
	long		coefficient[2];
	unsigned long	saturation[2], deadband[2];
};

struct mh_force_status {
	unsigned long	generation;
	int		available[MH_FORCE_EFFECTS], playing[MH_FORCE_EFFECTS];
};

/* Provider support, not a promise that a physical wheel is attached. */
int	mh_input_has_force(const struct mh_input_provider *provider);

/*
 * Lazily creates supported effects on the input owner's selected joystick.
 * Initial effects are not played. An absent device is success with no available
 * effects. Generation changes when effects are recreated. No hidden fallback.
 */
enum mh_error_code	mh_input_force_poll(struct mh_input *input,
	const struct mh_force_effect initial[MH_FORCE_EFFECTS],
	struct mh_force_status *output, struct mh_error *error_out);

/*
 * Changed/start masks index the effect kinds above. Start must be a subset
 * of changed; unchanged slots keep playing without an update or restart.
 */
enum mh_error_code	mh_input_force_apply(struct mh_input *input,
	const struct mh_force_effect effects[MH_FORCE_EFFECTS],
	unsigned int changed, unsigned int start, struct mh_error *error_out);

/* Stop and release effects; safe for absent/closed input. */
void	mh_input_force_stop(struct mh_input *input);

#endif
