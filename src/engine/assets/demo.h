#ifndef MH_DEMO_H
#define MH_DEMO_H

#include <stddef.h>

#include "engine/core/error.h"

/* Legacy format size; query header_size() for a versioned view. */
#define MH_DEMO_HEADER_BYTES 4012
#define MH_DEMO_30_HEADER_BYTES 6705
#define MH_DEMO_MAX_HEADER_BYTES 6713
/* Legacy format capacity; query slots() for a versioned view. */
#define MH_DEMO_PLAYERS 8
#define MH_DEMO_MAX_PLAYERS 13
#define MH_DEMO_CROSSING_ROWS 27
#define MH_DEMO_VEHICLE_BYTES 30
#define MH_DEMO_FRAME_HEADER_BYTES 8
#define MH_DEMO_NAME_BYTES 16
#define MH_DEMO_TEAM_BYTES 5

/* Zero-initialized recording metadata retains the original 2.5 format. */
enum mh_demo_version {
	MH_DEMO_VERSION_25,
	MH_DEMO_VERSION_30,
	/* Native timing v1; 3.0 player layout plus explicit timing metadata. */
	MH_DEMO_VERSION_40
};

struct mh_demo_player {
	unsigned long	active;
	char		name[MH_DEMO_NAME_BYTES + 1];
	char		team[MH_DEMO_TEAM_BYTES + 1];
	char		car[MH_DEMO_NAME_BYTES + 1];

	/* MDE 3.0 adds the recorded horn name; absent in 2.5. */
	char		horn[MH_DEMO_NAME_BYTES + 1];
	unsigned long	colors[4];

	/*
	 * Gear mode and control type (engine/input/control_type.h). Preserve
	 * the full disk words, including unknown device values.
	 */
	unsigned long	gear_mode, control_type;
	unsigned long	crossings[MH_DEMO_CROSSING_ROWS][4];
};

struct mh_demo {
	enum mh_demo_version	version;

	/*
	 * Native MDE 4.0 stores the effective Hz; playback adopts it for live
	 * driving. Recorded vehicles remain pose samples, not an input replay.
	 * Legacy 2.5/3.0 files have no timing metadata (both fields are zero).
	 */
	unsigned int		timing_version, simulation_rate;
	const unsigned char	*data;
	size_t			size, frame_bytes, frame_count;
	unsigned long		flags, laps;
	unsigned int		player_count, local_player;
	char			track[MH_DEMO_NAME_BYTES + 1];
	struct mh_demo_player	players[MH_DEMO_MAX_PLAYERS];
};

struct mh_demo_frame {
	unsigned long		flags, milliseconds;
	unsigned int		active_mask, unavailable_mask;

	/*
	 * Indexed by original slot, including entrants retired during
	 * recording. NULL only for slots absent from the initial roster. Each
	 * body is the original 30-byte vehicle codec, also used by
	 * engine/network/protocol.h.
	 */
	const unsigned char	*vehicles[MH_DEMO_MAX_PLAYERS];
};

/*
 * Borrow immutable file bytes; they must outlive the demo and its frames.
 * Accept MDE 2.5 (eight slots), 3.0 and native 4.0 (thirteen slots). Native 4.0
 * requires a supported timing version and simulation rate. Validate version,
 * roster, initial 64-frame block and whole-frame size. Strings receive an extra
 * terminator; raw fields/tails remain in data. No interpretation of lap limits,
 * crossing order, flag extensions or clock wrap is made here. Vehicle payload
 * decoding is the consumer's job. Output is unchanged on error.
 */
enum mh_error_code	mh_demo_read(const unsigned char *data, size_t size,
	struct mh_demo *output, struct mh_error *error_out);
enum mh_error_code	mh_demo_frame_read(const struct mh_demo *demo,
	size_t index, struct mh_demo_frame *output, struct mh_error *error_out);

/* Zero for an unsupported version. */
size_t		mh_demo_header_size(enum mh_demo_version version);
unsigned int	mh_demo_slots(enum mh_demo_version version);

/* Metadata prefix before the finalizable checkpoint tables; zero if unknown. */
size_t	mh_demo_crossings_offset(enum mh_demo_version version);

/*
 * Fixed-width, lossless header encoding, including string tails and all
 * crossing words. Does not inspect borrowed frames or require 64 frames.
 * Validates the same roster and u32 disk bounds; output is atomic.
 */
enum mh_error_code	mh_demo_header_write(const struct mh_demo *demo,
	unsigned char *data, size_t size, struct mh_error *error_out);

#endif
