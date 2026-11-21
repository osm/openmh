#ifndef MH_SPATIAL_QUEUE_H
#define MH_SPATIAL_QUEUE_H
#include "engine/audio/spatial_listener.h"
#include "engine/audio/spatial_playback.h"
#include "engine/audio/spatial_source.h"
#define MH_SPATIAL_QUEUE_PENDING 32
#define MH_SPATIAL_QUEUE_VOICES 16

/* Borrowed voices, one-based slots. Zero-initialize before use. */
struct mh_spatial_queue_voice {
	struct mh_spatial_source_input		input;
	struct mh_spatial_source_state		source;
	struct mh_spatial_playback_state	playback;
	unsigned long	fade_in_samples, fade_out_samples;
	unsigned int				slot;
	int	loaded, has_buffer, loop, retrigger;
};

struct mh_spatial_queue {
	struct mh_spatial_queue_voice	*pending[MH_SPATIAL_QUEUE_PENDING];
	struct mh_spatial_queue_voice	*voices[MH_SPATIAL_QUEUE_VOICES];
};

/*
 * Synchronous callbacks must not reenter the scheduler. Status retains the
 * frame-local playing/looping history when a consumer leaves it untouched.
 * Profile adapters may update profile-owned capabilities before status.
 */
struct mh_spatial_queue_sink {
	void							*context;
	void (*status)(void *, struct mh_spatial_queue_voice *,
		struct mh_spatial_playback_status		*);
	void (*source)(void *, const struct mh_spatial_queue_voice *,
		const struct mh_spatial_source_update		*);
	void (*playback)(void *, const struct mh_spatial_queue_voice *,
		const struct mh_spatial_playback_request	*);
};

struct mh_spatial_queue_frame {
	struct mh_spatial_listener_input	listener;
	const float				*anchor;
	int					secondary_bound;
	const struct mh_clock_source		*clock;
	float					master_gain;

	/* Explicit seed and resulting scratch; shared across both passes. */
	struct mh_spatial_source_parameters	parameters;
};

/*
 * Bound-source scheduling with shared spatial playback. Pending entries drain
 * before active slots (including newly admitted entries). Gain/pan scratch is
 * threaded through both passes; source updates precede eligibility rejection.
 * Stop/clear retain inactive guards. Null voice is a no-op; request overflow is
 * ignored. The profile owns listener updates after the scheduling pass.
 * Frame parameters must be initialized; positive elapsed is required whenever
 * a bound spatial source can be prepared. No allocation or device I/O.
 */
void	mh_spatial_queue_request(struct mh_spatial_queue *,
	struct mh_spatial_queue_voice *, const struct mh_spatial_queue_sink *);
void	mh_spatial_queue_stop(struct mh_spatial_queue *,
	struct mh_spatial_queue_voice *, const struct mh_spatial_queue_sink *);
void	mh_spatial_queue_clear(struct mh_spatial_queue *,
	const struct mh_spatial_queue_sink *);
void	mh_spatial_queue_step(struct mh_spatial_queue *,
	struct mh_spatial_queue_frame *, const struct mh_spatial_queue_sink *);
#endif
