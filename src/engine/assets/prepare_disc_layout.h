#ifndef MH_PREPARE_DISC_LAYOUT_H
#define MH_PREPARE_DISC_LAYOUT_H

#include "engine/archive/disc.h"

enum {
	MH_RETAIL_DISC_TRACKS = 11,
	MH_DISC_TRACK_NAME_SIZE = 256,
	MH_DISC_LAYOUT_TEXT_LIMIT = 16384
};

struct mh_disc_track {
	char		file[MH_DISC_TRACK_NAME_SIZE];
	unsigned long	offset, length;
};

/* Zero lengths mean whole files in the separate-track CUE layout. */
struct mh_disc_layout {
	struct mh_disc_track	tracks[MH_RETAIL_DISC_TRACKS];
	unsigned long		image_size;
	enum mh_disc_format	data_format;
	int			big_endian_audio;
};

/* Separate-track CUE or single-file cdrdao TOC; output survives failure. */
enum mh_error_code	mh_disc_layout_parse(const void *data, size_t size,
	struct mh_disc_layout *output, struct mh_error *error_out);

#endif
