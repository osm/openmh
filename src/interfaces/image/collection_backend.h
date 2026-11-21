#ifndef MH_IMAGE_COLLECTION_BACKEND_H
#define MH_IMAGE_COLLECTION_BACKEND_H

#include "interfaces/image/collection.h"

/* Descriptor, table and context are borrowed through the last handle close. */
struct mh_image_source_provider {
	const struct mh_image_source_ops	*ops;
	void					*context;
};

/* Callbacks follow collection.h ownership and output-preservation rules. */
struct mh_image_source_ops {
	/*
	 * Clean partial state on failure; successful open receives one close.
	 */
	enum mh_error_code (*open)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, void **output,
		struct mh_error	*error_out);
	enum mh_error_code (*read)(void *context, void *collection,
		const char *name, struct mh_file *output,
		struct mh_error	*error_out);
	void			(*close)(void *context, void *collection);
};

/*
 * Wrap backend state for format-specific constructors outside resource lookup.
 * Success transfers state to close(); failure leaves state with the caller.
 * Provider lifetime and callback rules are the same as collection_open().
 */
enum mh_error_code	mh_image_collection_create(
	const struct mh_image_source_provider *provider, void *state,
	struct mh_image_collection **output, struct mh_error *error_out);

#endif
