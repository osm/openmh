#ifndef MH_AUDIO_HORN_SOURCE_BACKEND_H
#define MH_AUDIO_HORN_SOURCE_BACKEND_H

#include "interfaces/audio/horn_source.h"

/*
 * Context is borrowed. Publish a complete catalog with 1..MH_HORNS entries,
 * unique case-insensitive display names and terminated names/sample references.
 * Preserve binary32 factors, original default/index ordering and the <None>
 * silent-sample marker. Gains satisfy mh_horn_parameters' accepted ranges.
 */
struct mh_horn_source {
	void				*context;
	enum mh_error_code (*load)(void *context,
		const struct mh_resource_source *resources, const char *root,
		struct mh_horn_catalog	*output, struct mh_error *error_out);
};

#endif
