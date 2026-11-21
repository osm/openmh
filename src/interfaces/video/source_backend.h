#ifndef MH_VIDEO_SOURCE_BACKEND_H
#define MH_VIDEO_SOURCE_BACKEND_H

#include "interfaces/video/source.h"

struct mh_video_source_provider {
	const struct mh_video_source_ops	*ops;
	void					*context;
};

/* Callbacks follow source.h ownership and output-preservation rules. */
struct mh_video_source_ops {
	enum mh_error_code (*resolve)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, char *output, size_t capacity,
		int		*found, struct mh_error *error_out);
	enum mh_error_code (*read)(void *context,
		const struct mh_resource_source *resources,
		const char *reference, int collection, unsigned int index,
		struct mh_file	*output, struct mh_error *error_out);
};

#endif
