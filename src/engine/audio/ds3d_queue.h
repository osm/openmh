#ifndef MH_DS3D_QUEUE_H
#define MH_DS3D_QUEUE_H

#include "engine/audio/ds3d_listener.h"
#include "engine/audio/ds3d_playback.h"
#include "engine/audio/ds3d_source.h"
#include "engine/audio/spatial_queue.h"

/*
 * Borrowed voices must remain live while either array uses them. Slot is
 * one-based; zero means unassigned. Pending voices are already marked active.
 */
struct mh_ds3d_voice {
	/* First member: scheduler callbacks recover this profile owner. */
	struct mh_spatial_queue_voice	common;
};

struct mh_ds3d_queue {
	struct mh_spatial_listener_state	listener;
	struct mh_spatial_queue			common;
};

/*
 * Policy consumers, not a device implementation. Callbacks are synchronous and
 * must not mutate the queue or voice definitions. Status may leave its in/out
 * value untouched on failure, reproducing the original frame-local history.
 * Only playing/looping are status outputs; eligibility belongs to preparation.
 * Source updates precede admission, including when preparation rejects a voice.
 */
struct mh_ds3d_queue_sink {
	void							*context;
	void (*status)(void *context, const struct mh_ds3d_voice *voice,
		struct mh_spatial_playback_status		*status);
	void (*source)(void *context, const struct mh_ds3d_voice *voice,
		const struct mh_spatial_source_update		*update);
	void (*playback)(void *context, const struct mh_ds3d_voice *voice,
		const struct mh_spatial_playback_request	*request);
	void (*listener)(
		void	*context, const struct mh_spatial_listener_update *update);
};

/*
 * Zero-initialize queue/voices before use. Slots are owned by the queue; source
 * inputs obey ds3d_source.h, playback inputs/clock obey ds3d_playback.h. The 3D
 * buffer flag input.has_buffer is separate from the playback buffer flag. All
 * four sink callbacks are required. There is no allocation or device I/O.
 *
 * Request silently ignores unloaded/active non-retrigger sounds and overflow.
 * Stop/clear preserve the original inactive guards. Null voice is a no-op. Step
 * drains pending entries, visits active slots, then updates the listener. Newly
 * admitted voices are updated again in that same active-slot pass.
 *
 * Published logarithmic gain/pan feed the shared scratch for subsequent voices.
 * Listener elapsed supplies every source step; it must be positive when a bound
 * spatial source can be prepared. Zero elapsed gates only the listener. Anchor
 * and listener position are distinct original inputs. Stop/clear retain
 * listener history. Sink callbacks must not reenter any queue operation. Caller
 * must initialize frame.parameters before stepping the queue.
 */
void	mh_ds3d_queue_request(struct mh_ds3d_queue *queue,
	struct mh_ds3d_voice *voice, const struct mh_ds3d_queue_sink *sink);
void	mh_ds3d_queue_stop(struct mh_ds3d_queue *queue,
	struct mh_ds3d_voice *voice, const struct mh_ds3d_queue_sink *sink);
void	mh_ds3d_queue_clear(struct mh_ds3d_queue *queue,
	const struct mh_ds3d_queue_sink *sink);
void	mh_ds3d_queue_step(struct mh_ds3d_queue *queue,
	struct mh_spatial_queue_frame *frame,
	const struct mh_ds3d_queue_sink *sink);

#endif
