#ifndef MH_WEBGL_ERROR_H
#define MH_WEBGL_ERROR_H

#include "engine/core/error.h"

/* Consume the first pending API error after a resource operation. */
enum mh_error_code	mh_webgl_check_error(struct mh_error *error_out);

/*
 * After deleting lost objects, retire their API errors before rebuilding.
 * Never use this to discard errors from creation or ordinary drawing.
 */
void	mh_webgl_discard_lost_object_errors(void);

#endif
