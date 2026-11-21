#ifndef MH_EAX_QUEUE_H
#define MH_EAX_QUEUE_H

#include "engine/audio/eax_playback.h"
#include "engine/audio/eax_source.h"

#define MH_EAX_PENDING 32

/*
 * Borrowed voice and sample name must remain live while either array uses it.
 * Slot is one-based; zero means unassigned. Pending voices are already active.
 */
struct mh_eax_voice {
	struct mh_spatial_source_input	input;
	struct mh_spatial_source_state	source;
	struct mh_eax_playback_state	playback;
	const char			*name;
	unsigned long			fade_in_samples, fade_out_samples;
	unsigned int			slot;
	int				loaded, has_buffer, loop, retrigger;
};

struct mh_eax_queue {
	struct mh_eax_voice	*pending[MH_EAX_PENDING];
	struct mh_eax_voice	*voices[MH_EAX_VOICES];
};

/*
 * Policy consumers, not a device implementation. Callbacks are synchronous and
 * must not mutate the queue or voice definitions. Status may leave its in/out
 * value untouched on failure, reproducing the original frame-local history.
 * Only playing/looping are status outputs; eligibility belongs to preparation.
 * Source updates precede admission, including when preparation rejects a voice.
 */
struct mh_eax_queue_sink {
	void						*context;
	void (*status)(void *context, const struct mh_eax_voice *voice,
		struct mh_eax_playback_status		*status);
	void (*source)(void *context, const struct mh_eax_voice *voice,
		const struct mh_spatial_source_update	*update);
	void (*playback)(void *context, const struct mh_eax_voice *voice,
		const struct mh_eax_playback_request	*request);
};

struct mh_eax_queue_frame {
	const struct mh_eax_source_listener	*listener;
	const struct mh_clock_source		*clock;
	float					master_gain;

	/* Explicit seed and resulting scratch; shared across both passes. */
	struct mh_spatial_source_parameters	parameters;
};

/*
 * Zero-initialize queue/voices before use. Slots are owned by the queue; source
 * inputs obey eax_source.h, playback inputs/clock obey eax_playback.h. The 3D
 * buffer flag input.has_buffer is separate from the playback buffer flag. All
 * three sink callbacks are required. There is no allocation or device I/O.
 *
 * Request silently ignores unloaded/active non-retrigger sounds and overflow.
 * Stop/clear preserve the original inactive guards. Null voice is a no-op. Step
 * drains pending entries then visits active slots in ascending order. Newly
 * admitted voices are updated again in that same active-slot pass.
 *
 * Published logarithmic gain feeds the shared scratch for subsequent voices.
 * Caller must initialize frame.parameters before stepping the queue.
 */
void	mh_eax_queue_request(struct mh_eax_queue *queue,
	struct mh_eax_voice *voice, const struct mh_eax_queue_sink *sink);
void	mh_eax_queue_stop(struct mh_eax_queue *queue,
	struct mh_eax_voice *voice, const struct mh_eax_queue_sink *sink);
void	mh_eax_queue_clear(struct mh_eax_queue *queue,
	const struct mh_eax_queue_sink *sink);
void	mh_eax_queue_step(struct mh_eax_queue *queue,
	struct mh_eax_queue_frame *frame, const struct mh_eax_queue_sink *sink);

#endif
