#ifndef MH_A3D20_EFFECTS_H
#define MH_A3D20_EFFECTS_H

#include "engine/audio/a3d20_pcm.h"

/* Returned terminated names are cache keys, borrowed until PCM destruction. */
typedef const char *(*mh_a3d20_sample_name)(
	void *, const struct mh_audio_pcm *);

/*
 * Bridge caller-owned producer banks to one A3D 2.0 queue. The producer pool
 * holds requests/activity only; its software scheduler must not run here.
 * Samples, names and the parsed world are borrowed until PCM destruction. Clear
 * retains the exact-name payload cache. Prepared views own no bytes. The
 * borrowed clock and sample-name context remain live until retirement.
 * Serialize every operation with the output device's callback exclusion.
 */
struct mh_a3d20_effects {
	struct mh_a3d20_queue		queue;
	struct mh_a3d20_pcm		pcm;
	struct mh_a3d20_voice		voices[MH_SOUND_RECORDS];
	struct mh_audio_pcm		waves[MH_SOUND_RECORDS];

	/*
	 * Immutable first payload per exact filename; duplicate voices borrow
	 * it.
	 */
	struct mh_audio_pcm		samples[MH_SOUND_RECORDS];
	const char			*names[MH_SOUND_RECORDS];
	size_t				sample_count;
	float				positions[MH_SOUND_RECORDS][3];

	mh_a3d20_sample_name		sample_name;
	void				*name_context;
	struct mh_a3d20_queue_frame	frame;
	long double			serviced_ms;
	int				listener_ready, serviced;
};

enum mh_error_code	mh_a3d20_effects_init(unsigned long rate, double gain,
	const struct mh_clock_source *clock, const struct mh_world *world,
	mh_a3d20_sample_name sample_name, void *name_context,
	struct mh_a3d20_effects *effects, struct mh_error *error_out);
void			mh_a3d20_effects_listener(
	struct mh_a3d20_effects *effects,
	const struct mh_sound_listener *listener);

/* Consume explicit stops immediately; no admission or PCM advancement. */
void			mh_a3d20_effects_stops(struct mh_a3d20_effects *effects,
	const struct mh_sound_pool *pool);
void			mh_a3d20_effects_clear(
	struct mh_a3d20_effects *effects);
void			mh_a3d20_effects_stop_device(
	struct mh_a3d20_effects *effects);
enum mh_error_code	mh_a3d20_effects_update(
	struct mh_a3d20_effects *effects, struct mh_sound_pool *pool,
	struct mh_mixer *const banks[], unsigned int count,
	struct mh_error *error_out);

#endif
