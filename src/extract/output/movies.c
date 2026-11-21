#include <stdlib.h>
#include <string.h>

#include "backends/video/ffmpeg_encode.h"
#include "backends/video/ffmpeg_provider.h"
#include "engine/assets/divi.h"
#include "engine/assets/divi_data.h"
#include "engine/assets/divi_layout.h"
#include "engine/core/binary.h"
#include "extract/output/compact.h"

static enum mh_error_code
encode_clip(struct mh_file *clip, mh_media_encode_check check, void *context,
	struct mh_error *error_out)
{
	static const unsigned char webm[] = {0x1a, 0x45, 0xdf, 0xa3};
	struct mh_video_decoder *decoder;
	struct mh_file encoded;
	enum mh_error_code status;

	/* Existing compact clips must never go through another lossy encode. */
	if (clip->size >= sizeof(webm) &&
		memcmp(clip->data, webm, sizeof(webm)) == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	decoder = NULL;
	status = mh_video_decoder_open(mh_video_ffmpeg_decoder(), clip->data,
		clip->size, 1, &decoder, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_video_ffmpeg_encode(
			decoder, check, context, &encoded, error_out);

	mh_video_decoder_close(decoder);

	if (status == MH_ERROR_NONE) {
		free(clip->data);
		*clip = encoded;
	}

	return status;
}

static enum mh_error_code
append_clip(struct mh_file *collection, const struct mh_file *clip,
	unsigned int index, struct mh_error *error_out)
{
	unsigned char *next;

	if (clip->size > MH_RESOURCE_READ_LIMIT - collection->size)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"converted movie collection exceeds its size limit");

	next = realloc(collection->data, collection->size + clip->size);

	if (next == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot grow converted movie collection");

	collection->data = next;
	mh_write_u32le(next + MH_DIVI_INDEX_PREFIX_BYTES +
			MH_DIVI_PAYLOAD_OFFSET + index * MH_DIVI_ENTRY_BYTES,
		(unsigned long)collection->size);
	memcpy(next + collection->size, clip->data, clip->size);
	collection->size += clip->size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_extract_compact_movies(const struct mh_resource_source *resources,
	const char *path, mh_media_encode_check check, void *context,
	struct mh_file *output, struct mh_error *error_out)
{
	struct mh_video_divi_index index;
	struct mh_file collection, clip;
	unsigned long size;
	unsigned int i;
	enum mh_error_code status;

	collection.size = MH_VIDEO_DIVI_HEADER_SIZE;
	collection.data = malloc(collection.size);

	if (collection.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate converted movie index");

	status = mh_resource_source_size(resources, path, &size, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_read_at(resources, path, 0,
			collection.data, collection.size, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_video_divi_index(collection.data, collection.size,
			size, &index, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < index.count; i++) {
		clip.data = NULL;

		if (check != NULL)
			status = check(context, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_video_divi_read(
				resources, path, i, &clip, error_out);

		if (status == MH_ERROR_NONE)
			status = encode_clip(&clip, check, context, error_out);

		if (status == MH_ERROR_NONE)
			status = append_clip(&collection, &clip, i, error_out);

		free(clip.data);
	}

	if (status != MH_ERROR_NONE) {
		free(collection.data);
		return status;
	}

	*output = collection;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
