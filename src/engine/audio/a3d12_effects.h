#ifndef MH_A3D12_EFFECTS_H
#define MH_A3D12_EFFECTS_H

#include "engine/audio/a3d12_pcm.h"

/*
 * Bridge caller-owned producer banks to one A3D 1.2 queue. The producer pool
 * holds requests/activity only; its software scheduler must not run here.
 * Samples and banks are borrowed until clear. Prepared views own no bytes. The
 * borrowed clock remains live until this owner is retired. Serialize every
 * operation with the output device's callback exclusion.
 */
struct mh_a3d12_effects {
	struct mh_a3d12_queue		queue;
	struct mh_a3d12_pcm		pcm;
	struct mh_a3d12_voice		voices[MH_SOUND_RECORDS];
	struct mh_audio_pcm		waves[MH_SOUND_RECORDS];
	struct mh_spatial_queue_frame	frame;
	long double			serviced_ms;
	int				listener_ready, serviced;
};

enum mh_error_code	mh_a3d12_effects_init(unsigned long rate, double gain,
	const struct mh_clock_source *clock, struct mh_a3d12_effects *effects,
	struct mh_error *error_out);
void			mh_a3d12_effects_listener(
	struct mh_a3d12_effects *effects,
	const struct mh_sound_listener *listener);

/* Consume explicit stops immediately; no admission or PCM advancement. */
void			mh_a3d12_effects_stops(struct mh_a3d12_effects *effects,
	const struct mh_sound_pool *pool);
void			mh_a3d12_effects_clear(
	struct mh_a3d12_effects *effects);
void			mh_a3d12_effects_stop_device(
	struct mh_a3d12_effects *effects);
enum mh_error_code	mh_a3d12_effects_update(
	struct mh_a3d12_effects *effects, struct mh_sound_pool *pool,
	struct mh_mixer *const banks[], unsigned int count,
	struct mh_error *error_out);

#endif
