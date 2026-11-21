#ifndef MH_EAX_LISTENER_H
#define MH_EAX_LISTENER_H

/* Portable game-side EAX 1 listener policy; no device or DSP implementation. */
struct mh_eax_listener_state {
	float	position[3], reverb;
};

struct mh_eax_listener_input {
	float	elapsed, reverb;
	float	position[3], front[3], up[3];
};

struct mh_eax_listener_update {
	int	listener_changed, reverb_changed;
	float	reverb;
	float	position[3], front[3], up[3], velocity[3];
};

/*
 * Caller supplies finite values and nonnegative seconds. Zero elapsed leaves
 * retained state unchanged and requests no listener commit. A zero retained
 * position suppresses first-frame velocity. Reverb compares the unscaled input
 * with the cached scaled value deliberately. Cold process state is all zero;
 * device recreation does not clear the reverb cache.
 */
void	mh_eax_listener_step(struct mh_eax_listener_state *state,
	const struct mh_eax_listener_input *input,
	struct mh_eax_listener_update *output);

#endif
