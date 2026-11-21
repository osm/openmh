#ifndef MH_A3D20_LISTENER_H
#define MH_A3D20_LISTENER_H

struct mh_a3d20_listener_state {
	float	position[3];
};

struct mh_a3d20_listener_input {
	float	elapsed;
	float	position[3], front[3], up[3];
};

struct mh_a3d20_listener_update {
	int	changed;
	float	position[3], front[3], up[3], velocity[3];
};

/*
 * NULL input skips listener requests and preserves history. Otherwise inputs
 * must be finite, with positive elapsed. Unlike A3D 1.2, the original does not
 * gate on elapsed. All-zero history seeds position. Output negates Z; history
 * remains in game coordinates. Bind position via geometry
 * push/translate/bind-listener/pop before source updates; afterward submit
 * orientation then velocity. Call this step after preparing sources, since
 * those consume the preceding listener position. Scene clear/flush and geometry
 * collection belong to the caller. Request failures do not undo retained
 * history.
 */
void	mh_a3d20_listener_step(struct mh_a3d20_listener_state *state,
	const struct mh_a3d20_listener_input *input,
	struct mh_a3d20_listener_update *output);

#endif
