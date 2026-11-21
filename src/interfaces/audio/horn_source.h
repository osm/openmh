#ifndef MH_AUDIO_HORN_SOURCE_H
#define MH_AUDIO_HORN_SOURCE_H

#include "engine/core/error.h"
#include "interfaces/audio/horn.h"

struct mh_horn_source;
struct mh_resource_source;

/*
 * Load an ordered catalog of validated inline definitions. Borrow source and
 * resources during the call only; output owns its values. Sample references
 * are logical audio resources. Keep original order for indexed settings and
 * profile RNG selection. Failure leaves output unchanged.
 */
enum mh_error_code	mh_horn_catalog_load_from(
	const struct mh_horn_source *source,
	const struct mh_resource_source *resources, const char *root,
	struct mh_horn_catalog *output, struct mh_error *error_out);

#endif
