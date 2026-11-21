#ifndef MH_AUDIO_MUSIC_CATALOG_H
#define MH_AUDIO_MUSIC_CATALOG_H

#include "engine/core/error.h"

struct mh_resource_source;
struct mh_audio_decoder_provider;

/* Original duration-table bound, distinct from the playable CD track range. */
#define MH_MUSIC_CATALOG_LAST_TRACK 30

/*
 * Physical track indices 0..30. Track 1 is the unextracted data track;
 * its duration remains unknown (zero). Initialize the catalog to zero.
 */
struct mh_music_catalog {
	unsigned long	seconds[MH_MUSIC_CATALOG_LAST_TRACK + 1];
	unsigned int	last_track;
};

/*
 * Scan track lengths. The selected decoder inspects metadata without loading
 * PCM and resolves logical track references. Provider and resources remain
 * borrowed for the call. NULL directory uses GAME_ROOT/Music. Audio starts at
 * 2; stop at the first missing/subsecond track, or after querying 30.
 * last_track is queried_track-1 even if track 30 exists. Preserve unqueried
 * tail lengths on a rescan. Missing directories, malformed files and I/O errors
 * are atomic errors; the frontend may report them and retain an empty catalog
 * without disabling effects. Metadata must describe 44.1 or 48 kHz, stereo,
 * 16-bit PCM. Encoded-file limits belong to the decoder.
 */
enum mh_error_code	mh_music_catalog_scan_from(
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *directory, struct mh_music_catalog *catalog,
	struct mh_error *error_out);

#endif
