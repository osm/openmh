#ifndef MH_RETRO_SOURCE_BACKEND_H
#define MH_RETRO_SOURCE_BACKEND_H

#include "interfaces/retro/source.h"

/*
 * Borrowed context. Supply one decoded part, validating counts/indices, finite
 * values and terminated text. Return NOT_FOUND only when this provider has no
 * data for the request; malformed data must return an error.
 */
struct mh_retro_source {
	void					*context;
	enum mh_error_code (*load)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, enum mh_retro_part part,
		union mh_retro_part_data	*output, struct mh_error *error_out);
};

#endif
