#ifndef MH_AUDIO_MIXER_H
#define MH_AUDIO_MIXER_H

#include "engine/audio/sound_pool.h"
#include "engine/audio/sound_spatial.h"
#include "engine/core/error.h"
#include "interfaces/audio/pcm.h"

/*
 * Logical records per native bank, independent of the original queue size.
 * The race bank retains legacy voices and adds five engine/horn pairs.
 */
#define MH_MIXER_VOICES 45

/* Native PCM input/output bounds, independent of device frequency clamps. */
#define MH_MIXER_RATE_MIN 4000
#define MH_MIXER_RATE_MAX 192000
#define MH_MIXER_FRAMES_MAX 8192

struct mh_mixer_voice {
	const struct mh_audio_pcm	*wave;
	double				cursor, pitch, left, right;

	/* Source levels precede software ear saturation. */
	double				gain, pan;

	/*
	 * Optional native PCM transition; ordinary software playback leaves
	 * zero.
	 */
	double				target_pitch, target_left, target_right;
	size_t				transition;

	/* Exclusive end; equal bounds disable looping. */
	size_t				loop_start, loop_end;
	int				active;
};

/*
 * Owned spatial snapshots. Parameters change in producers; positions/listener
 * can be published independently after motion. The effects callback prepares
 * gains at admission/refresh, not publication.
 */
struct mh_mixer_spatial {
	double	position[3], volume, near_distance, far_distance;
	int	bound;
};

struct mh_mixer {
	struct mh_mixer_voice		voices[MH_MIXER_VOICES];
	unsigned long			rate;
	double				gain;
	unsigned long	flags[MH_MIXER_VOICES], priority[MH_MIXER_VOICES];
	unsigned long	fade_in[MH_MIXER_VOICES], fade_out[MH_MIXER_VOICES];

	/* Optional borrowed shared effects pool; must outlive mixer use. */
	struct mh_sound_pool		*pool;

	/* Zero-based start of this bank's window. */
	unsigned int			record_base;
	struct mh_sound_listener	listener;
	struct mh_mixer_spatial		spatial[MH_MIXER_VOICES];
};

/*
 * Native backend-neutral mixer: caller owns decoded PCM views and bytes.
 * Unbound mixers render fixed slots. Bound logical banks submit requests
 * to one shared pool; effects.c renders the admitted physical channels.
 * mixer_pcm.c emits interleaved stereo floats in [-1,1], with linear
 * interpolation and final saturation. This is not a DirectSound bit-exact
 * emulation.
 */
enum mh_error_code	mh_mixer_init(unsigned long rate, double gain,
	struct mh_mixer *mixer, struct mh_error *error_out);

/*
 * Bind an inactive logical bank to an exclusive MH_MIXER_VOICES pool window.
 * Bound banks issue requests; only the separate physical mixer renders PCM.
 */
enum mh_error_code	mh_mixer_bind(struct mh_mixer *mixer,
	struct mh_sound_pool *pool, unsigned int base,
	struct mh_error *error_out);
enum mh_error_code	mh_mixer_policy(struct mh_mixer *mixer,
	unsigned int voice, unsigned long flags, unsigned long priority,
	struct mh_error *error_out);

/*
 * Explicit start/restart. Consumers decide whether their original record
 * flags permit a retrigger before calling this function. Pitch is a finite
 * multiplier in the binary32 domain, including negative/zero; retain it in
 * the record and saturate source frequency at playback to 0..200000 Hz.
 * The physical effects converter applies the original zero-step fallback.
 */
enum mh_error_code	mh_mixer_play(struct mh_mixer *mixer,
	unsigned int voice, const struct mh_audio_pcm *wave, double pitch,
	double left, double right, size_t loop_start, size_t loop_end,
	struct mh_error *error_out);

/*
 * Copy an inactive shared definition and request it without stopping prior
 * physical playback. Bound pools retain old queue/channel aliases; unbound
 * diagnostic mixers have only one physical slot per voice.
 */
enum mh_error_code	mh_mixer_copy(struct mh_mixer *mixer,
	unsigned int voice, const struct mh_audio_pcm *wave, double pitch,
	double left, double right, size_t loop_start, size_t loop_end,
	struct mh_error *error_out);
enum mh_error_code	mh_mixer_adjust(struct mh_mixer *mixer,
	unsigned int voice, double pitch, double left, double right,
	struct mh_error *error_out);
void			mh_mixer_stop(struct mh_mixer *mixer,
	unsigned int voice);

/*
 * No cursor advances while paused. Frames <=MH_MIXER_FRAMES_MAX.
 * Output unchanged on failure; render silence when paused.
 */
enum mh_error_code	mh_mixer_render(struct mh_mixer *mixer, float *stereo,
	size_t frames, int paused, struct mh_error *error_out);

/*
 * Add a separate bus (e.g. CD music) to existing finite [-1,1] samples,
 * then saturate. Paused calls leave the existing samples and cursor alone.
 */
enum mh_error_code	mh_mixer_add(struct mh_mixer *mixer, float *stereo,
	size_t frames, int paused, struct mh_error *error_out);

#endif
