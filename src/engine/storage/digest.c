#include "engine/storage/digest.h"
#include "interfaces/storage/resource.h"

enum {
	DIGEST_READ_BYTES = 16384
};

enum mh_error_code
mh_resource_sha256(const struct mh_resource_source *source, const char *path,
	char digest[MH_SHA256_TEXT_SIZE], unsigned long *size,
	struct mh_error *error_out)
{
	struct mh_sha256 hash;
	unsigned char buffer[DIGEST_READ_BYTES], bytes[MH_SHA256_BYTES];
	unsigned long length, offset;
	size_t count;
	enum mh_error_code status;

	if (digest == NULL || size == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing resource identity output");

	status = mh_resource_source_size(source, path, &length, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_sha256_init(&hash);
	offset = 0;

	while (offset < length) {
		count = sizeof(buffer);

		if (length - offset < count)
			count = (size_t)(length - offset);

		status = mh_resource_source_read_at(
			source, path, offset, buffer, count, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_sha256_update(&hash, buffer, count);
		offset += count;
	}

	mh_sha256_finish(&hash, bytes);
	mh_sha256_text(bytes, digest);
	*size = length;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
