#ifndef MH_STORAGE_DIGEST_H
#define MH_STORAGE_DIGEST_H

#include "engine/core/error.h"
#include "engine/core/sha256.h"

struct mh_resource_source;

/*
 * Hash in bounded chunks through the selected resource provider. The resource
 * must remain unchanged during the call. Outputs remain unchanged on failure.
 */
enum mh_error_code	mh_resource_sha256(
	const struct mh_resource_source *source, const char *path,
	char digest[MH_SHA256_TEXT_SIZE], unsigned long *size,
	struct mh_error *error_out);

#endif
