#ifndef MH_MEDIA_ENCODE_H
#define MH_MEDIA_ENCODE_H

#include "engine/core/error.h"

/* Optional cancellation check between bounded pieces of encoding work. */
typedef enum mh_error_code (*mh_media_encode_check)(
	void *context, struct mh_error *error_out);

#endif
