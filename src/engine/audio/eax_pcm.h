#ifndef MH_EAX_PCM_H
#define MH_EAX_PCM_H

#include "engine/audio/eax_queue.h"
#include "engine/audio/eax_reverb.h"
#include "engine/audio/spatial_pcm.h"

/*
 * Native EAX queue consumer: shared positional PCM plus an owned reverb bus.
 * Bound voices remain live until binding replacement or owner retirement,
 * even after queue clear. WAV definitions and PCM bytes are borrowed until
 * queue clear/teardown.
 * Queue policy retains EAX's 24 slots, gain tuning and listener-relative
 * sources. All buffers are positional mono, including input.spatial == 0; that
 * flag selects source positioning policy, not the original EAX buffer kind.
 * Serialize queue, listener and render operations. No device or race ownership.
 */
struct mh_eax_pcm {
	struct mh_spatial_pcm	output;
	struct mh_reverb	*reverb;

	/* Final bus volume, 0..1, including existing tails; default 1. */
	double			volume;
};

/*
 * Gain is fixed PCM headroom before reverb. Zero-initialize the owner; failed
 * init leaves it unchanged. Destroy successful owners before reinitializing.
 */
enum mh_error_code	mh_eax_pcm_init(unsigned long rate, double gain,
	struct mh_eax_pcm *pcm, struct mh_error *error_out);

/* Stop/clear the queue and detach callbacks before destroying this owner. */
void			mh_eax_pcm_destroy(struct mh_eax_pcm *pcm);
enum mh_error_code	mh_eax_pcm_bind(struct mh_eax_pcm *pcm,
	unsigned int record_index, const struct mh_eax_voice *voice,
	const struct mh_audio_pcm *wave, struct mh_error *error_out);
void			mh_eax_pcm_sink(struct mh_eax_pcm *pcm,
	struct mh_eax_queue_sink *sink);

/* Apply after the complete queue step, using the listener request. */
enum mh_error_code	mh_eax_pcm_listener(struct mh_eax_pcm *pcm,
	const struct mh_eax_listener_update *update,
	struct mh_error *error_out);

/* After queue clear, discard remaining PCM/tails; retain bindings/listener. */
void	mh_eax_pcm_clear(struct mh_eax_pcm *pcm);

/*
 * Emits effects only; owner adds music/UI and applies final saturation.
 * Pause freezes both source cursors and reverb. Completion queries report
 * source playback, never the continuing reverb tail. Frames use mixer bounds.
 * Callback errors remain latched until reinitialization; stopping still works.
 */
enum mh_error_code	mh_eax_pcm_render(struct mh_eax_pcm *pcm, float *stereo,
	size_t frames, int paused, struct mh_error *error_out);

#endif
