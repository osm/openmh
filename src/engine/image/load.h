#ifndef MH_IMAGE_LOAD_H
#define MH_IMAGE_LOAD_H

#include "interfaces/image/decoder.h"

struct mh_resource_source;
struct mh_file;

/*
 * Resolve logical image references with the selected decoder's suffix. A NULL
 * extension retains original names; another extension replaces only the final
 * filename suffix. No fallback to an original encoding. Source and provider are
 * borrowed during the call; decoded pixel allocations are owned by the caller.
 * Outputs remain unchanged on failure. The resource limit bounds encoded bytes;
 * the decoder independently bounds decoded pixels.
 *
 * Ordinary file read without provider dispatch, for composition backends.
 */
enum mh_error_code	mh_image_file_read(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_file *file,
	struct mh_error *error_out);

enum mh_error_code	mh_image_path_from(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *path, size_t capacity,
	struct mh_error *error_out);

/* Missing entries set found=0; enumeration and malformed-path errors fail. */
enum mh_error_code	mh_image_path_find_from(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *path, size_t capacity, int *found,
	struct mh_error *error_out);

/* Resolve the selected suffix and read owned bytes; caller frees file->data. */
enum mh_error_code	mh_image_read_from(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_file *file,
	struct mh_error *error_out);
enum mh_error_code	mh_image_load_from(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, const struct mh_image_decode_options *options,
	struct mh_image *output, struct mh_image_palette *palette_output,
	struct mh_error *error_out);
enum mh_error_code	mh_image_load_indexed_from(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_indexed_image *output,
	struct mh_error *error_out);

#endif
