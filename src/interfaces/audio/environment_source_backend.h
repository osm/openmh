#ifndef MH_ENV_SOUND_SOURCE_BACKEND_H
#define MH_ENV_SOUND_SOURCE_BACKEND_H

#include "interfaces/audio/environment_source.h"

/*
 * Keep fixed positions and object/collision classes distinct, with original
 * units and order. Count is bounded by MH_ENV_SOUNDS; sample names are
 * terminated. Collision classes are 0..15; object classes are unsigned 32-bit
 * selectors. Positions, distance ranges, volume and pitch are finite decoded
 * values. Context is borrowed. Publish only complete, validated values.
 */
struct mh_env_sound_source {
	void			*context;
	enum mh_error_code (*load)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_env_sounds *output,
		struct mh_error	*error_out);
};

#endif
