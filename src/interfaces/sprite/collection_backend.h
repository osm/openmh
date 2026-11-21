#ifndef MH_SPRITE_COLLECTION_BACKEND_H
#define MH_SPRITE_COLLECTION_BACKEND_H

#include "interfaces/sprite/collection.h"

/* Descriptor, operations and context remain borrowed through handle close. */
struct mh_sprite_source_provider {
	const struct mh_sprite_source_ops	*ops;
	void					*context;
};

/* Callbacks follow collection.h ownership and failure-preservation rules. */
struct mh_sprite_source_ops {
	/*
	 * Clean partial state on failure; every successful open receives close.
	 */
	enum mh_error_code (*open)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, enum mh_picture_format format,
		void	**output, struct mh_error *error_out);
	enum mh_error_code (*info)(void *context, void *state, const char *name,
		struct mh_sprite_info	*output, struct mh_error *error_out);
	enum mh_error_code (*palette)(void *context, void *state,
		struct mh_image_palette	*output, struct mh_error *error_out);
	enum mh_error_code (*indexed)(void *context, void *state,
		const char *name, unsigned int frame,
		struct mh_sprite_indexed	*output, struct mh_error *error_out);
	enum mh_error_code (*rgba)(void *context, void *state, const char *name,
		unsigned int frame, struct mh_image *output,
		struct mh_error			*error_out);
	void	(*close)(void *context, void *state);
};

/* Success transfers state to close; failure leaves it with the caller. */
enum mh_error_code	mh_sprite_collection_create(
	const struct mh_sprite_source_provider *provider, void *state,
	struct mh_sprite_collection **output, struct mh_error *error_out);

#endif
