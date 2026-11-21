#include <string.h>

#include "engine/audio/sample_path.h"
#include "engine/storage/asset_path.h"
#include "interfaces/audio/decoder_backend.h"

#define SAMPLE_PATH_BYTES 1024

enum mh_error_code
mh_audio_sample_file_path(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *reference, char *path, size_t capacity,
	struct mh_error *error_out)
{
	const char *extension, *scan, *suffix, *basename;
	char relative[SAMPLE_PATH_BYTES];
	size_t stem, length;

	if (reference == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing audio sample reference");

	extension = mh_audio_decoder_extension(decoder);

	if (extension == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"audio decoding is unavailable");

	basename = reference;
	suffix = NULL;

	for (scan = reference; *scan != 0; scan++) {
		if (*scan == '/' || *scan == '\\') {
			basename = scan + 1;
			suffix = NULL;
		} else if (*scan == '.')
			suffix = scan;
	}

	if (*basename == 0 || suffix == basename ||
		(suffix != NULL && suffix[1] == 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio sample reference");

	stem = (size_t)((suffix == NULL ? scan : suffix) - reference);
	length = strlen(extension);

	if (stem >= sizeof(relative) - length - 1)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"audio sample reference is too long");

	memcpy(relative, reference, stem);
	relative[stem] = '.';
	memcpy(relative + stem + 1, extension, length + 1);

	return mh_asset_path_from(
		source, root, relative, path, capacity, error_out);
}

enum mh_error_code
mh_audio_sample_path_from(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *path, size_t capacity,
	struct mh_error *error_out)
{
	if (decoder != NULL && decoder->resolve != NULL)
		return decoder->resolve(decoder->context, resources, root,
			reference, path, capacity, error_out);

	return mh_audio_sample_file_path(
		decoder, resources, root, reference, path, capacity, error_out);
}
