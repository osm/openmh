#ifndef MH_SPATIAL_LISTENER_H
#define MH_SPATIAL_LISTENER_H

struct mh_spatial_listener_state {
	float	position[3];
};

struct mh_spatial_listener_input {
	float	elapsed;
	float	position[3], front[3], up[3];
};

struct mh_spatial_listener_update {
	int	changed;
	float	position[3], front[3], up[3], velocity[3];
};

/*
 * Shared EAX/DS3D/A3D 1.2 listener motion. Finite positions and positive
 * elapsed. An all-zero previous position suppresses velocity, including signed
 * zero. Retain the current position even if the eventual driver request fails.
 */
void	mh_spatial_listener_velocity(float previous[3], const float position[3],
	float elapsed, float velocity[3]);

/* Deferred position/orientation update; zero elapsed preserves history. */
void	mh_spatial_listener_step(struct mh_spatial_listener_state *state,
	const struct mh_spatial_listener_input *input,
	struct mh_spatial_listener_update *output);

#endif
