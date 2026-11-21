#ifndef MH_ARCHIVE_DISC_H
#define MH_ARCHIVE_DISC_H

#include "engine/core/error.h"

struct mh_resource_source;

enum {
	MH_DISC_BLOCK_BYTES = 2048,
	MH_DISC_SECTOR_BYTES = 2352,
	MH_DISC_DATA_OFFSET = 16
};

enum mh_disc_format {
	MH_DISC_ISO,
	MH_DISC_MODE1
};

/* Borrows source and path; the image must remain unchanged through reads. */
struct mh_disc_image {
	const struct mh_resource_source	*source;
	const char			*path;
	enum mh_disc_format		format;
	unsigned long			blocks;
};

enum mh_error_code	mh_disc_image_init(
	const struct mh_resource_source *source, const char *path,
	enum mh_disc_format format, struct mh_disc_image *output,
	struct mh_error *error_out);

/* Limit reads to the data track at offset zero; zero bytes means whole file. */
enum mh_error_code	mh_disc_image_init_track(
	const struct mh_resource_source *source, const char *path,
	enum mh_disc_format format, unsigned long track_bytes,
	struct mh_disc_image *output, struct mh_error *error_out);

/* Read logical data bytes; raw Mode 1 headers are checked before copying. */
enum mh_error_code	mh_disc_image_read(const struct mh_disc_image *image,
	unsigned long offset, void *output, size_t size,
	struct mh_error *error_out);

#endif
