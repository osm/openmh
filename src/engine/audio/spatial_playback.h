#ifndef MH_SPATIAL_PLAYBACK_H
#define MH_SPATIAL_PLAYBACK_H

struct mh_clock_source;

struct mh_spatial_playback_state {
	unsigned long	started, fading;
	int		active;
};

struct mh_spatial_playback_input {
	int		loaded, has_buffer, loop, spatial;
	unsigned long	fade_in_samples, fade_out_samples;
	float		gain, prepared_gain, pan, frequency;
};

struct mh_spatial_playback_status {
	int	playing, looping, eligible;
};

enum mh_spatial_playback_action {
	MH_SPATIAL_PLAYBACK_NONE,
	MH_SPATIAL_PLAYBACK_START,
	MH_SPATIAL_PLAYBACK_UPDATE,
	MH_SPATIAL_PLAYBACK_STOP
};

struct mh_spatial_playback_request {
	enum mh_spatial_playback_action	action;
	unsigned long			frequency;
	long				volume, pan;
	int				loop, set_pan;
	float				log_gain, log_pan;
};

/*
 * Shared 3.1 DS3D/A3D 1.2 source playback policy, without SDK objects or
 * audible processing. Spatial playback uses source gain; nonspatial uses
 * prepared_gain (including master gain) scaled by .6. No sample-name
 * adjustments apply. Nonspatial requests send logarithmic pan before
 * frequency/volume; spatial ones do not. Start then asks for cursor zero and
 * Play(loop), retaining admission's active flag. Stop clears timestamps/active.
 * Failed driver requests do not roll back. log_gain/log_pan are the scratch
 * left by those calculations; log_pan is only meaningful when set_pan is true.
 * Queue owners must propagate this scratch.
 *
 * Inputs are finite; gains nonnegative, frequency positive, counts/history u32.
 * Clock is borrowed, history not ahead of it; see spatial_levels.h for timing.
 */
void	mh_spatial_playback_start(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	const struct mh_clock_source *clock,
	struct mh_spatial_playback_request *output);
void	mh_spatial_playback_step(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	const struct mh_spatial_playback_status *status,
	const struct mh_clock_source *clock,
	struct mh_spatial_playback_request *output);
void	mh_spatial_playback_stop(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	struct mh_spatial_playback_request *output);

#endif
