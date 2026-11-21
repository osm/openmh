#ifndef MH_CATALOG_SOURCE_BACKEND_H
#define MH_CATALOG_SOURCE_BACKEND_H

#include "interfaces/catalog/source.h"

/*
 * Return validated definitions and stable consecutive IDs. Preserve source
 * precision and optional-field groups. All outputs are published on success
 * only. NULL callbacks report unsupported operations. Context is borrowed.
 */
struct mh_catalog_source {
	void			*context;
	enum mh_error_code (*count_cars)(void *context,
		const struct mh_resource_source *resources, const char *root,
		unsigned int	*output, struct mh_error *error_out);
	enum mh_error_code (*load_car)(void *context,
		const struct mh_resource_source *resources, const char *root,
		unsigned int number, struct mh_car *output,
		struct mh_error	*error_out);
	enum mh_error_code (*count_tracks)(void *context,
		const struct mh_resource_source *resources, const char *root,
		unsigned int	*output, struct mh_error *error_out);
	enum mh_error_code (*load_track)(void *context,
		const struct mh_resource_source *resources, const char *root,
		unsigned int number, struct mh_track *output,
		struct mh_error	*error_out);
};

#endif
