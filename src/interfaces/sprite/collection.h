#ifndef MH_SPRITE_COLLECTION_H
#define MH_SPRITE_COLLECTION_H

#include "engine/core/error.h"
#include "interfaces/sprite/pixels.h"

struct mh_resource_source;
struct mh_sprite_source_provider;
struct mh_sprite_collection;

struct mh_sprite_info {
	unsigned int	frames, width, height;
};

/*
 * Open a logical animation collection. Format identifies the authored color
 * model (indexed, RGB555 or RGB565); it never specifies file encoding or row
 * layout. A replacement source may store equivalent pixels in another format.
 * The provider maps the logical reference to physical files or containers.
 * Provider and resources are borrowed until close; root/reference are
 * call-only. Success transfers one handle; failure preserves output. Diagnostic
 * messages belong to the provider, not returned pixels or handle state, and
 * remain valid after close until the next provider operation.
 */
enum mh_error_code	mh_sprite_collection_open(
	const struct mh_sprite_source_provider *provider,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, enum mh_picture_format format,
	struct mh_sprite_collection **output, struct mh_error *error_out);

/*
 * Names are exact, case-sensitive logical names. Missing sprites report
 * NOT_FOUND. Metadata dimensions are pixels, never encoded row-byte widths.
 * All frames in a sprite have these dimensions. Output is preserved on error.
 */
enum mh_error_code	mh_sprite_collection_info(
	const struct mh_sprite_collection *collection, const char *name,
	struct mh_sprite_info *output, struct mh_error *error_out);

/* Original palette order, including duplicate colors; no index remapping. */
enum mh_error_code	mh_sprite_collection_palette(
	const struct mh_sprite_collection *collection,
	struct mh_image_palette *output, struct mh_error *error_out);

/*
 * Decoded frames are owned independently of the collection; caller frees pixels
 * or rgba. Indexed coverage is separate: written index zero stays opaque.
 * RGBA retains the selected packed-color interpretation and transparent gaps.
 * Unsupported views return UNSUPPORTED. Errors preserve output.
 */
enum mh_error_code	mh_sprite_collection_indexed(
	const struct mh_sprite_collection *collection, const char *name,
	unsigned int frame, struct mh_sprite_indexed *output,
	struct mh_error *error_out);
enum mh_error_code	mh_sprite_collection_rgba(
	const struct mh_sprite_collection *collection, const char *name,
	unsigned int frame, struct mh_image *output,
	struct mh_error *error_out);
void			mh_sprite_collection_close(
	struct mh_sprite_collection *collection);

#endif
