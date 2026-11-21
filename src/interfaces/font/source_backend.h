#ifndef MH_FONT_SOURCE_BACKEND_H
#define MH_FONT_SOURCE_BACKEND_H

#include "interfaces/font/source.h"

struct mh_font_source_ops {
	enum mh_error_code (*load)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_font *output,
		struct mh_error	*error_out);
	enum mh_error_code (*load_program)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_font_program *output,
		struct mh_error	*error_out);
};

/* Callbacks follow source.h; descriptor, ops and context are borrowed. */
struct mh_font_source_provider {
	const struct mh_font_source_ops	*ops;
	void				*context;
};

#endif
