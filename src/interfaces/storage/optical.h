#ifndef MH_STORAGE_OPTICAL_H
#define MH_STORAGE_OPTICAL_H

#include "interfaces/storage/resource.h"

enum {
	MH_OPTICAL_TRACK_LIMIT = 99,
	MH_OPTICAL_DATA_BYTES = 2048,
	MH_OPTICAL_AUDIO_BYTES = 2352,
	MH_OPTICAL_READ_SECTORS = 16
};

enum mh_optical_track_kind {
	MH_OPTICAL_DATA,
	MH_OPTICAL_AUDIO
};

struct mh_optical_track {
	unsigned int			number;
	enum mh_optical_track_kind	kind;
	unsigned long			sectors;
};

struct mh_optical_disc;

/* Called before each device read; returning an error cancels the read. */
typedef enum mh_error_code (*mh_optical_progress)(void *context,
	unsigned int track, unsigned long sector, unsigned long total,
	struct mh_error *error_out);

void				 mh_optical_disc_close(
	struct mh_optical_disc *disc);
size_t				 mh_optical_disc_count(
	const struct mh_optical_disc *disc);
const struct mh_optical_track	*mh_optical_disc_track(
	const struct mh_optical_disc *disc, size_t index);

/*
 * Borrowed, read-only source with paths "track01" through "track99".
 * Data tracks contain cooked 2048-byte sectors; audio is signed 16-bit
 * stereo PCM, 44100 Hz, little endian. Track boundaries are INDEX 01.
 * Keep the disc open until all source users finish. Access is serialized.
 */
const struct mh_resource_source	*mh_optical_disc_resources(
	struct mh_optical_disc *disc);
void				 mh_optical_disc_set_progress(
	struct mh_optical_disc *disc, mh_optical_progress progress,
	void *context);

#endif
