#ifndef MH_A3D20_PLAYBACK_H
#define MH_A3D20_PLAYBACK_H

struct mh_clock_source;

struct mh_a3d20_playback_state {
	unsigned long	started, fading;
	int		active, skip_update;
};

struct mh_a3d20_playback_input {
	const char	*name;
	int		loaded, has_source, loop, spatial;
	unsigned long	sample_rate, fade_in_samples, fade_out_samples;
	float		gain, prepared_gain, pitch;
};

struct mh_a3d20_playback_status {
	int	playing, looping, eligible;
};

enum mh_a3d20_playback_action {
	MH_A3D20_PLAYBACK_NONE,
	MH_A3D20_PLAYBACK_START,
	MH_A3D20_PLAYBACK_UPDATE,
	MH_A3D20_PLAYBACK_STOP
};

struct mh_a3d20_playback_request {
	enum mh_a3d20_playback_action	action;
	float				pitch, gain;
	int				loop;
};

/*
 * A3D 2.0 playback policy. No device calls or DSP. Inputs have a terminated
 * name, finite nonnegative gains, positive pitch and sample rate, u32
 * counts/history and a nonnegative clock below MH_CLOCK_MS_LIMIT. History must
 * not be ahead of the clock. The clock is sampled per original operation. Pitch
 * is passed unchanged; gain is linear with an upper limit of 1.
 *
 * Start submits pitch, gain, cursor zero, Play(loop), retaining queue-owned
 * active state and setting skip_update even if Play fails. The next active pass
 * clears that flag without querying status or preparing the source. Stop clears
 * active, timestamps and skip_update. Updates submit pitch/gain.
 *
 * Gain fade durations use sample_rate*pitch; release expiry uses pitch alone,
 * preserving the original discrepancy. Request gain becomes shared scratch.
 */
void	mh_a3d20_playback_start(struct mh_a3d20_playback_state *state,
	const struct mh_a3d20_playback_input *input,
	const struct mh_clock_source *clock,
	struct mh_a3d20_playback_request *output);
void	mh_a3d20_playback_step(struct mh_a3d20_playback_state *state,
	const struct mh_a3d20_playback_input *input,
	const struct mh_a3d20_playback_status *status,
	const struct mh_clock_source *clock,
	struct mh_a3d20_playback_request *output);
void	mh_a3d20_playback_stop(struct mh_a3d20_playback_state *state,
	const struct mh_a3d20_playback_input *input,
	struct mh_a3d20_playback_request *output);

#endif
