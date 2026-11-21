#ifndef MH_PREPARE_CUE_H
#define MH_PREPARE_CUE_H

#include "engine/assets/prepare_disc_layout.h"

/* The supplied release: one Mode 1 BIN and ten separate PCM BIN files. */
struct mh_cue {
	char	files[MH_RETAIL_DISC_TRACKS][MH_DISC_TRACK_NAME_SIZE];
};

/* Supports FILE/TRACK/INDEX records, not general CUE-sheet directives. */
enum mh_error_code	mh_cue_parse(const void *bytes, size_t size,
	struct mh_cue *output, struct mh_error *error_out);

#endif
