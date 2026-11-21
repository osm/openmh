#include <stdlib.h>
#include <string.h>

#include "engine/core/sha256.h"
#include "extract/source/demo.h"
#include "interfaces/storage/resource.h"

void
mh_extract_demo_free(struct mh_extract_demo *demo)
{
	free(demo->file.data);
	memset(demo, 0, sizeof(*demo));
}

enum mh_error_code
mh_extract_demo_read(const struct mh_resource_source *source, const char *path,
	struct mh_extract_demo *output, struct mh_error *error_out)
{
	struct mh_extract_demo demo;
	struct mh_sha256 hash;
	unsigned char digest[MH_SHA256_BYTES];
	char text[MH_SHA256_TEXT_SIZE];
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing demo source output");

	status = mh_resource_source_read(source, path, &demo.file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_sha256_init(&hash);
	mh_sha256_update(&hash, demo.file.data, demo.file.size);
	mh_sha256_finish(&hash, digest);
	mh_sha256_text(digest, text);
	demo.package =
		mh_demo_package_find((unsigned long)demo.file.size, text);

	if (demo.package == NULL ||
		demo.package->cabinet_offset >= demo.file.size) {
		free(demo.file.data);
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"source is not one of the supplied demo packages");
	}

	*output = demo;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
