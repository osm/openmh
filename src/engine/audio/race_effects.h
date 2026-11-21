#ifndef MH_RACE_EFFECTS_H
#define MH_RACE_EFFECTS_H

#include "engine/audio/mixer.h"
#include "engine/audio/race_profile.h"

struct mh_clock_source;
struct mh_world;

/* Software ring counters; unbuffered profiles report zero. */
struct mh_race_effect_buffer_stats {
	unsigned long	fills, wraps, frames;
};

/* Private race-profile adapters, not an output-device backend contract. */
struct mh_race_effect_config {
	unsigned long			rate;
	double				gain;
	int				manual_service;

	struct mh_sound_pool		*pool;
	struct mh_mixer			*const *banks;
	unsigned int			bank_count;

	const struct mh_clock_source	*clock;
	const struct mh_world		*world;

	const char	*(*sample_name)(void *, const struct mh_audio_pcm *);
	const char	*(*sample_path)(void *, const struct mh_audio_pcm *);
	void				*sample_context;
};

/* Shared native pre-DSP headroom for environmental processing. */
#define MH_RACE_ENVIRONMENT_HEADROOM .125

/*
 * The race owner allocates zeroed state and frees it after optional destroy.
 * Init borrows config referents (including the bank array), not config itself.
 * Destroy must accept partial initialization. Clear releases logical requests;
 * finish ends physical playback and discards buffered effects for results.
 * Listener, reset_clock, service_buffer and buffer_stats are optional; the
 * owner supplies no-op/zero defaults for profiles without those operations.
 * All operations run under the output device's callback exclusion, or while
 * it is stopped. No adapter owns the output device or producer sample bytes.
 */
struct mh_race_effect_ops {
	size_t					size;
	unsigned int				capacity;

	enum mh_error_code (*init)(void *state,
		const struct mh_race_effect_config *config,
		struct mh_error			*error_out);
	enum mh_error_code (*clear)(void *state, struct mh_sound_pool *pool,
		struct mh_mixer *const banks[], unsigned int count,
		struct mh_error			*error_out);
	enum mh_error_code	(*finish)(void *state, struct mh_error *error_out);
	void					(*destroy)(void *state);
	void					(*stop_device)(void *state);
	enum mh_error_code (*stops)(void *state,
		const struct mh_sound_pool	*pool, struct mh_error *error_out);

	void (*listener)(void *state, const struct mh_sound_listener *listener,
		float				reverb);
	void	(*gain)(void *state, double gain);
	void					(*reset_clock)(void *state);

	unsigned int	(*active_voices)(const void *state);
	void (*buffer_stats)(
		const void	*state, struct mh_race_effect_buffer_stats *output);

	enum mh_error_code (*service_buffer)(
		void	*state, struct mh_error *error_out);
	enum mh_error_code (*update)(void *state, struct mh_sound_pool *pool,
		struct mh_mixer *const banks[], unsigned int count,
		struct mh_error			*error_out);
	enum mh_error_code (*render)(void *state, float *samples, size_t frames,
		int	paused, struct mh_error *error_out);
};

const struct mh_race_effect_ops	*mh_race_profile_ops(int selection);

/* Adapter helpers; concrete mixers never cross the operations interface. */
unsigned int		mh_race_effect_active_voices(
	const struct mh_mixer *mixer);
enum mh_error_code	mh_race_effect_clear_pool(struct mh_sound_pool *pool,
	struct mh_mixer *const banks[], unsigned int count,
	struct mh_error *error_out);

#endif
