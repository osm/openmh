#ifndef MH_EAX_EFFECTS_H
#define MH_EAX_EFFECTS_H

#include "engine/audio/eax_pcm.h"

/* Returned terminated names are borrowed while the voice is queued/active. */
typedef const char *(*mh_eax_sample_name)(void *, const struct mh_audio_pcm *);

/*
 * Bridge caller-owned producer banks to one EAX queue. The producer pool holds
 * requests/activity only; its software scheduler must not run here. Samples and
 * banks are borrowed until clear. Prepared views own no bytes. The borrowed
 * clock and sample-name context remain live until retirement. Serialize every
 * operation with the output device's callback exclusion.
 */
struct mh_eax_effects {
	struct mh_eax_source_listener	listener;
	struct mh_eax_listener_input	listener_input;
	struct mh_eax_listener_state	listener_state;

	mh_eax_sample_name		sample_name;
	void				*name_context;
	struct mh_eax_queue		queue;
	struct mh_eax_pcm		pcm;
	struct mh_eax_voice		voices[MH_SOUND_RECORDS];
	struct mh_audio_pcm		waves[MH_SOUND_RECORDS];
	struct mh_eax_queue_frame	frame;
	long double			serviced_ms;
	int				serviced;
};

enum mh_error_code	mh_eax_effects_init(unsigned long rate, double gain,
	const struct mh_clock_source *clock, mh_eax_sample_name sample_name,
	void *name_context, struct mh_eax_effects *effects,
	struct mh_error *error_out);
void			mh_eax_effects_listener(struct mh_eax_effects *effects,
	const struct mh_sound_listener *listener, float reverb);

/* Consume explicit stops immediately; no admission or PCM advancement. */
void			mh_eax_effects_stops(struct mh_eax_effects *effects,
	const struct mh_sound_pool *pool);
void			mh_eax_effects_clear(struct mh_eax_effects *effects);
void			mh_eax_effects_stop_device(
	struct mh_eax_effects *effects);
enum mh_error_code	mh_eax_effects_update(struct mh_eax_effects *effects,
	struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, struct mh_error *error_out);

#endif
