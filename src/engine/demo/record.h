#ifndef MH_DEMO_RECORD_H
#define MH_DEMO_RECORD_H

#include "engine/assets/demo.h"
#include "engine/core/error.h"

enum {
	MH_DEMO_RING_FRAMES = 64,
	MH_DEMO_BLOCK_FRAMES = 32
};

#define MH_DEMO_MAX_FRAME_BYTES                                                \
	(MH_DEMO_FRAME_HEADER_BYTES +                                          \
		MH_DEMO_MAX_PLAYERS * MH_DEMO_VEHICLE_BYTES)
#define MH_DEMO_RECORD_BLOCK_BYTES                                             \
	(MH_DEMO_BLOCK_FRAMES * MH_DEMO_MAX_FRAME_BYTES)

struct mh_demo_record_sample {
	unsigned int	active_mask, unavailable_mask;

	/*
	 * Encoded vehicle bodies, indexed by initial slot.
	 * Bodies remain present when a recorded entrant becomes inactive.
	 */
	unsigned char	bodies[MH_DEMO_MAX_PLAYERS][MH_DEMO_VEHICLE_BYTES];
};

struct mh_demo_record_event {
	int		started;

	/* One ordinary frame, or 0..31 final padding frames. */
	unsigned int	frames;

	/* Zero unless a completed half-block must be appended. */
	size_t		size;
	unsigned char	data[MH_DEMO_RECORD_BLOCK_BYTES];
};

struct mh_demo_recorder {
	/* Owned metadata; no borrowed data/frame view. */
	struct mh_demo	header;

	unsigned char	ring[MH_DEMO_RING_FRAMES * MH_DEMO_MAX_FRAME_BYTES];
	unsigned long	anchor_ms, last_frame_ms, clock_ms;
	size_t		frame_count;
	unsigned int	ring_index, initial_mask;
	int		closed;
};

/*
 * Start recording outside attract mode and demo playback. Header strings/tails
 * are explicit fixed-width bytes. Caller writes mh_demo_header_write(header)
 * first, appends each returned block, then backfills the versioned crossing
 * tables. No FILE/SDL/OS dependency. All outputs are unchanged on failure.
 */
enum mh_error_code	mh_demo_record_begin(const struct mh_demo *header,
	struct mh_demo_recorder *output, struct mh_error *error_out);

/*
 * Use a monotonic, pause-aware 32-bit millisecond clock. A zero anchor
 * restarts timing. Record at most one snapshot per call, after more than
 * 62.5 ms since the last frame. Both masks are limited to the initial roster.
 * The caller supplies vehicle bodies encoded in recording format.
 */
enum mh_error_code	mh_demo_record_step(struct mh_demo_recorder *recorder,
	unsigned long clock_ms, const struct mh_demo_record_sample *sample,
	struct mh_demo_record_event *event, struct mh_error *error_out);

/*
 * Pad only the current half-block with current bodies, adding 1ms per frame
 * modulo u32. 2.5 writes ACTIVE bits only; 3.0 keeps unavailable bits too.
 * Backfill each table selected by crossing_mask, including inactive entrants.
 * Finalization closes this native controller. It does not synthesize a missing
 * initial 64 frames; callers must handle recordings too short for the original
 * loader.
 */
enum mh_error_code	mh_demo_record_finish(struct mh_demo_recorder *recorder,
	const struct mh_demo_record_sample *sample,
	const struct mh_demo_player *players, unsigned int crossing_mask,
	struct mh_demo_record_event *event, struct mh_error *error_out);

#endif
