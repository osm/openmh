#ifndef MH_ARCHIVE_ISO_H
#define MH_ARCHIVE_ISO_H

#include "engine/archive/disc.h"
#include "interfaces/archive/archive.h"

/*
 * Open contiguous ISO-9660/Joliet files through resource range reads.
 * source stays borrowed until close; path is copied. The image must remain
 * unchanged. Supports contiguous files and the ASCII subset of Joliet names
 * present on the supplied disc, not arbitrary optical-image extensions.
 */
enum mh_error_code	mh_iso_open(const struct mh_resource_source *source,
	const char *path, enum mh_disc_format format,
	struct mh_archive **output, struct mh_error *error_out);

/* As above, bounded to the first track; zero bytes means whole file. */
enum mh_error_code	mh_iso_open_track(
	const struct mh_resource_source *source, const char *path,
	enum mh_disc_format format, unsigned long track_bytes,
	struct mh_archive **output, struct mh_error *error_out);

#endif
