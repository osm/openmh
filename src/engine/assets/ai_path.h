#ifndef MH_ASSETS_AI_PATH_H
#define MH_ASSETS_AI_PATH_H

#include "engine/core/error.h"
#include "engine/navigation/ai_path.h"

/* Output unchanged on failure; free successful output with mh_ai_path_free. */
enum mh_error_code	mh_ai_path_dat_decode(const unsigned char *data,
	size_t size, struct mh_ai_path *output, struct mh_error *error_out);

#endif
