#ifndef MH_EXTRACT_SOURCE_INPUT_H
#define MH_EXTRACT_SOURCE_INPUT_H

#include "engine/core/error.h"

enum mh_extract_input_kind {
	MH_EXTRACT_INPUT_GAME,
	MH_EXTRACT_INPUT_DISC,
	MH_EXTRACT_INPUT_DEVICE,
	MH_EXTRACT_INPUT_FILE
};

struct mh_extract_input {
	/* Borrows the source argument. */
	const char			*path;
	enum mh_extract_input_kind	kind;
};

/*
 * Distinguish prepared games, copied discs and files before choosing a decoder.
 */
enum mh_error_code	mh_extract_input_identify(const char *path,
	struct mh_extract_input *output, struct mh_error *error_out);

#endif
