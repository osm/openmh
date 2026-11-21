#ifndef MH_EXTRACT_PATH_JOIN_H
#define MH_EXTRACT_PATH_JOIN_H

#include "engine/core/error.h"

/*
 * Join slash-separated paths; an empty root adds no slash and a trailing slash
 * is retained. The prefix variant reads exactly root_length bytes, permitting
 * a directory slice of an image filename. Output is owned, unchanged on error.
 * These helpers join names; archive path validation remains with the decoder.
 */
enum mh_error_code	mh_extract_join(const char *root, const char *relative,
	char **output, struct mh_error *error_out);
enum mh_error_code	mh_extract_join_prefix(const char *root,
	size_t root_length, const char *relative, char **output,
	struct mh_error *error_out);

#endif
