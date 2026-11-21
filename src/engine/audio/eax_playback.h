#ifndef MH_EAX_PLAYBACK_H
#define MH_EAX_PLAYBACK_H

struct mh_clock_source;

struct mh_eax_playback_state {
	unsigned long	started, fading;
	int		active;
};

struct mh_eax_playback_input {
	const char	*name;
	int		loaded, has_buffer, loop;
	unsigned long	fade_in_samples, fade_out_samples;
	float		gain, frequency;
};

struct mh_eax_playback_status {
	int	playing, looping, eligible;
};

enum mh_eax_playback_action {
	MH_EAX_PLAYBACK_NONE,
	MH_EAX_PLAYBACK_START,
	MH_EAX_PLAYBACK_UPDATE,
	MH_EAX_PLAYBACK_STOP
};

struct mh_eax_playback_request {
	enum mh_eax_playback_action	action;
	unsigned long			frequency;
	long				volume;
	int				loop;
	float				log_gain;
};

/*
 * 3.1 EAX playback policy; no audio device calls or PCM processing. Inputs:
 * terminated sample name, finite nonnegative gain, positive frequency, u32
 * sample counts/history and a working nonnegative millisecond clock below
 * MH_CLOCK_MS_LIMIT. Clock is borrowed and sampled for each timing operation.
 * History must not be ahead of that clock. Start retains active (the queue owns
 * admission), sets timestamps, then asks for frequency, volume, cursor zero and
 * Play(loop), in that order. Update asks for frequency then volume. Stop clears
 * active/timestamps and requests Stop. Request log_gain is the scratch gain
 * left by the original volume calculation. Failed device calls do not roll back
 * state; queue removal belongs to its owner.
 */
void	mh_eax_playback_start(struct mh_eax_playback_state *state,
	const struct mh_eax_playback_input *input,
	const struct mh_clock_source *clock,
	struct mh_eax_playback_request *output);
void	mh_eax_playback_step(struct mh_eax_playback_state *state,
	const struct mh_eax_playback_input *input,
	const struct mh_eax_playback_status *status,
	const struct mh_clock_source *clock,
	struct mh_eax_playback_request *output);
void	mh_eax_playback_stop(struct mh_eax_playback_state *state,
	const struct mh_eax_playback_input *input,
	struct mh_eax_playback_request *output);

#endif
