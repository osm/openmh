#ifndef MH_SETTINGS_DECIMAL_H
#define MH_SETTINGS_DECIMAL_H

#include "engine/core/error.h"
#define MH_SETTINGS_DECIMAL_BYTES 64

/*
 * Fixed formatting of promoted finite floats: 16 significant decimal digits,
 * ties away from zero, unsigned rounded zero, ASCII point. Supports 0..9
 * places; 64 bytes includes the terminator for every float. Output remains
 * unchanged on failure.
 */
enum mh_error_code	mh_settings_decimal(float value, unsigned int places,
	char output[MH_SETTINGS_DECIMAL_BYTES], struct mh_error *error_out);

#endif
