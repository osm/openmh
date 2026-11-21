#ifndef MH_IMAGE_COLLECTION_H
#define MH_IMAGE_COLLECTION_H

#include "engine/core/error.h"
#include "engine/core/file.h"

struct mh_resource_source;
struct mh_image_source_provider;
struct mh_image_collection;

/*
 * Open a logical image collection. Providers map its reference to their own
 * storage (an archive, directory, or other representation). Provider and
 * resource source remain borrowed until close; root/reference are call-only.
 * Success returns one owned handle. Failure leaves output unchanged.
 * Diagnostic text belongs to the provider, not handle state or image bytes;
 * it remains valid after close until the next provider operation.
 */
enum mh_error_code	mh_image_collection_open(
	const struct mh_image_source_provider *provider,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_image_collection **output,
	struct mh_error *error_out);

/*
 * Read a logical named image, independent of its physical filename or encoding.
 * Names retain the original ASCII case-insensitive matching convention. Success
 * transfers malloc-owned encoded bytes; caller frees data. Output is unchanged
 * on failure. Missing members report NOT_FOUND. No container offsets escape.
 */
enum mh_error_code	mh_image_collection_read(
	const struct mh_image_collection *collection, const char *name,
	struct mh_file *output, struct mh_error *error_out);

/* NULL is harmless. Previously returned image buffers remain valid. */
void	mh_image_collection_close(struct mh_image_collection *collection);

#endif
