#ifndef MH_PREPARE_CD_AUDIO_H
#define MH_PREPARE_CD_AUDIO_H

#include "engine/core/error.h"

struct mh_resource_source;
struct mh_data_directory;

/*
 * Copy signed little-endian 44.1 kHz stereo 16-bit PCM into a WAV container.
 * The supplied release stores one audio track per BINARY file. The source
 * must remain unchanged. No resampling, trimming or track renumbering occurs.
 * A failed copy leaves no destination file. Existing destinations are refused.
 */
enum mh_error_code	mh_cd_audio_extract(
	const struct mh_resource_source *source, const char *path,
	struct mh_data_directory *directory, unsigned int track,
	struct mh_error *error_out);

/*
 * Read a track range, swapping big-endian PCM when requested. Zero length
 * reads the rest of the file. Offsets need not be multiples of 2352: a
 * preceding cooked data track uses 2048-byte sectors.
 */
enum mh_error_code	mh_cd_audio_extract_track(
	const struct mh_resource_source *source, const char *path,
	unsigned long offset, unsigned long length, int big_endian,
	struct mh_data_directory *directory, unsigned int track,
	struct mh_error *error_out);

#endif
