#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "backends/archive/unshield_provider.h"
#include "backends/archive/unshield_zip.h"
#include "engine/archive/zip_layout.h"
#include "engine/core/binary.h"

#define ZIP_BYTE_LIMIT (128UL * 1024UL * 1024UL)

static enum mh_error_code
inflate_member(const unsigned char *bytes, unsigned long packed,
	unsigned long expanded, unsigned long checksum, struct mh_file *output,
	struct mh_error *error_out)
{
	z_stream stream;
	struct mh_file file;
	unsigned long actual_crc;
	int result, valid;

	file.size = (size_t)expanded;

	if (packed > UINT_MAX || expanded >= UINT_MAX ||
		expanded != file.size || file.size == (size_t)-1 ||
		expanded > ZIP_BYTE_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"ZIP member exceeds decoder size limit");

	file.data = malloc(file.size + 1);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory expanding ZIP member");

	memset(&stream, 0, sizeof(stream));
	stream.next_in = (Bytef *)bytes;
	stream.avail_in = (uInt)packed;
	stream.next_out = file.data;
	stream.avail_out = (uInt)expanded + 1;
	result = inflateInit2(&stream, -MAX_WBITS);

	if (result != Z_OK) {
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot initialize ZIP member decoder");
	}

	result = inflate(&stream, Z_FINISH);
	valid = result == Z_STREAM_END && stream.total_in == packed &&
		stream.total_out == expanded;
	inflateEnd(&stream);
	actual_crc = valid ? crc32(0, file.data, (uInt)file.size) : 0;

	if (!valid || actual_crc != checksum) {
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"ZIP member size or checksum mismatch");
	}

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_unshield_zip_member(const void *data, size_t size, const char *name,
	struct mh_file *output, struct mh_error *error_out)
{
	const unsigned char *bytes;
	unsigned long names, extra, packed, expanded, checksum;
	size_t start;

	bytes = data;

	if (bytes == NULL || output == NULL || name == NULL ||
		size < MH_ZIP_LOCAL_BYTES ||
		mh_read_u32le(bytes) != MH_ZIP_LOCAL_SIGNATURE ||
		mh_read_u16le(bytes + MH_ZIP_LOCAL_VERSION) !=
			MH_ZIP_VERSION_20 ||
		mh_read_u16le(bytes + MH_ZIP_LOCAL_FLAGS) !=
			MH_ZIP_DEFLATE_MAXIMUM ||
		mh_read_u16le(bytes + MH_ZIP_LOCAL_METHOD) != MH_ZIP_DEFLATED)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unexpected ZIP member header");

	names = mh_read_u16le(bytes + MH_ZIP_LOCAL_NAME_SIZE);
	extra = mh_read_u16le(bytes + MH_ZIP_LOCAL_EXTRA_SIZE);
	packed = mh_read_u32le(bytes + MH_ZIP_LOCAL_PACKED_SIZE);
	expanded = mh_read_u32le(bytes + MH_ZIP_LOCAL_SIZE);
	checksum = mh_read_u32le(bytes + MH_ZIP_LOCAL_CRC);
	start = MH_ZIP_LOCAL_BYTES;

	if (names != strlen(name) || names > size - start ||
		memcmp(bytes + start, name, (size_t)names) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unexpected ZIP member name");

	start += (size_t)names;

	if (extra > size - start || packed > size - start - extra)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "truncated ZIP member");

	start += (size_t)extra;

	return inflate_member(
		bytes + start, packed, expanded, checksum, output, error_out);
}

enum mh_error_code
mh_unshield_zip_cabinet(const void *data, size_t size, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing ZIP cabinet output");

	status = mh_unshield_zip_member(
		data, size, MH_UNSHIELD_CABINET_NAME, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (file.size < 4 || memcmp(file.data, "ISc(", 4) != 0) {
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"ZIP cabinet signature mismatch");
	}

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
