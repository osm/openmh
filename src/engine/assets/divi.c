#include <string.h>

#include "engine/assets/divi.h"
#include "engine/assets/divi_layout.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

enum mh_error_code
mh_video_divi_index(const unsigned char *header, size_t size,
	unsigned long archive_size, struct mh_video_divi_index *output,
	struct mh_error *error_out)
{
	struct mh_video_divi_index index;
	unsigned long count, payload_end;
	unsigned int i;
	const unsigned char *entry_bytes;

	if (header == NULL || output == NULL ||
		size < MH_VIDEO_DIVI_HEADER_SIZE ||
		archive_size < MH_VIDEO_DIVI_HEADER_SIZE ||
		archive_size > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid movie archive header");

	if (memcmp(header, "DIVI", 4) != 0 ||
		mh_read_u32le(header + MH_DIVI_INDEX_SIZE_OFFSET) !=
			MH_VIDEO_DIVI_HEADER_SIZE)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported movie archive");

	count = mh_read_u32le(header + MH_DIVI_ENTRY_COUNT_OFFSET);

	if (count == 0 || count > MH_VIDEO_DIVI_ENTRY_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid movie count");

	memset(&index, 0, sizeof(index));
	index.count = (unsigned int)count;

	for (i = 0; i < index.count; i++) {
		entry_bytes = header + MH_DIVI_INDEX_PREFIX_BYTES +
			i * MH_DIVI_ENTRY_BYTES;
		index.entries[i].kind = mh_read_u32le(entry_bytes);
		index.entries[i].offset =
			mh_read_u32le(entry_bytes + MH_DIVI_PAYLOAD_OFFSET);
		index.entries[i].flags =
			mh_read_u32le(entry_bytes + MH_DIVI_FLAGS_OFFSET);
		payload_end = i + 1 < index.count
			? mh_read_u32le(
				entry_bytes + MH_DIVI_NEXT_PAYLOAD_OFFSET)
			: archive_size;

		if (index.entries[i].offset < MH_VIDEO_DIVI_HEADER_SIZE ||
			payload_end <= index.entries[i].offset ||
			payload_end > archive_size)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid movie payload range");

		index.entries[i].size = payload_end - index.entries[i].offset;
	}

	*output = index;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
