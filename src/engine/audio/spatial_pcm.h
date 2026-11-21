#ifndef MH_SPATIAL_PCM_H
#define MH_SPATIAL_PCM_H

#include "engine/audio/mixer.h"
#include "engine/audio/spatial_playback.h"
#include "engine/audio/spatial_source.h"

struct mh_spatial_pcm_listener {
	float	position[3], front[3], up[3], velocity[3];
};

struct mh_spatial_pcm_voice {
	const void			*identity;
	const struct mh_audio_pcm	*wave;
	struct mh_spatial_source_update	source;
	double				near_distance, far_distance;
	double				linear_gain, pitch_ratio;
	int				linear;
	unsigned long			frequency;
	long				volume, pan;
	int				spatial, primed;

	/* One-based PCM slot; zero until started. */
	unsigned int			channel;
};

/* Linear gain and ratio pitch, without DirectSound logarithmic conversion. */
struct mh_spatial_pcm_linear_request {
	enum mh_spatial_playback_action	action;
	double				gain, pitch;
	int				loop;
};

/*
 * Shared native stereo PCM mechanism; profile adapters own queue policy. Owns
 * PCM cursors and snapshots, borrows voice definitions and prepared WAV views.
 * Logical bindings span all producer banks; PCM channels are acquired on
 * start and retained until stop, including completed samples pending status.
 * No allocation, device access, HRTF or environmental processing. Speaker
 * panning and bounded Doppler are native approximations, not driver emulation.
 */
struct mh_spatial_pcm {
	struct mh_mixer			mixer;
	double				gain;

	/* Zero disables smoothing; race output selects a short native ramp. */
	size_t				transition_frames;
	struct mh_spatial_pcm_voice	voices[MH_SOUND_RECORDS];
	struct mh_spatial_pcm_listener	listener;

	/* First callback error is retained until reinitialization. */
	struct mh_error			error;
};

/* Borrowed identities are compared only, never dereferenced by this module. */
enum mh_error_code	mh_spatial_pcm_init(unsigned long rate, double gain,
	struct mh_spatial_pcm *output, struct mh_error *error_out);

/* Adapter rejects queued/active rebinding before calling this function. */
enum mh_error_code	mh_spatial_pcm_bind(struct mh_spatial_pcm *output,
	unsigned int record_index, const void *identity,
	const struct mh_spatial_source_input *input,
	const struct mh_audio_pcm *wave, struct mh_error *error_out);

/* Synchronous adapter operations; latch the first missing-binding error. */
void	mh_spatial_pcm_source(struct mh_spatial_pcm *output,
	const void *identity, const struct mh_spatial_source_update *update);
void	mh_spatial_pcm_status(struct mh_spatial_pcm *output,
	const void *identity, int *playing, int *looping);
void	mh_spatial_pcm_playback(struct mh_spatial_pcm *output,
	const void *identity,
	const struct mh_spatial_playback_request *request);
void	mh_spatial_pcm_playback_linear(struct mh_spatial_pcm *output,
	const void *identity,
	const struct mh_spatial_pcm_linear_request *request);

/*
 * Prepare active voices in record order before rendering PCM. A later
 * preparation error retains earlier voice changes without writing stereo.
 */
enum mh_error_code	mh_spatial_pcm_render(struct mh_spatial_pcm *output,
	float *stereo, size_t frames, int paused, struct mh_error *error_out);

/* Same preparation, with an optional native PCM processor before saturation. */
struct mh_mixer_processor;
enum mh_error_code	mh_spatial_pcm_render_processed(
	struct mh_spatial_pcm *output, float *stereo, size_t frames, int paused,
	const struct mh_mixer_processor *processor, struct mh_error *error_out);

#endif
