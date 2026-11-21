#include <stdlib.h>

#include "engine/image/collection_load.h"

enum mh_error_code
mh_image_collection_load(const struct mh_image_decoder_provider *decoder,
	const struct mh_image_collection *collection, const char *name,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette, struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	status = mh_image_collection_read(collection, name, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_image_decode(decoder, file.data, file.size, options, output,
		palette, error_out);
	free(file.data);

	return status;
}

enum mh_error_code
mh_image_collection_load_indexed(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_image_collection *collection, const char *name,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	status = mh_image_collection_read(collection, name, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_image_decode_indexed(
		decoder, file.data, file.size, output, error_out);
	free(file.data);

	return status;
}
