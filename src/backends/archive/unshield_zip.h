#ifndef MH_UNSHIELD_ZIP_H
#define MH_UNSHIELD_ZIP_H

#include "engine/core/error.h"
#include "engine/core/file.h"

#define MH_UNSHIELD_CABINET_NAME "data1.cab"

/* Known SFX member: local ZIP header, bounded Deflate bytes and CRC. */
enum mh_error_code	mh_unshield_zip_member(const void *data, size_t size,
	const char *name, struct mh_file *output, struct mh_error *error_out);

#endif
