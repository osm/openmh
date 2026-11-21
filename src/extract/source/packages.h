#ifndef MH_EXTRACT_SOURCE_PACKAGES_H
#define MH_EXTRACT_SOURCE_PACKAGES_H

#include "engine/assets/prepare_demo_recipe.h"
#include "engine/core/error.h"

struct mh_resource_source;
struct mh_file;

#define MH_RETAIL_CABINET_BYTES 38628865UL

enum mh_package_container {
	MH_PACKAGE_CABINET,
	MH_PACKAGE_ZIP_CABINET
};

struct mh_demo_package {
	struct mh_demo_recipe		recipe;
	const char			*sha256;
	unsigned long			size, cabinet_offset, wrapper_offset;
	enum mh_package_container	container;
};

enum mh_patch_kind {
	MH_PATCH_CUMULATIVE,
	MH_PATCH_EARLIER,
	MH_PATCH_EXECUTABLE_ONLY
};

struct mh_patch_package {
	const char		*version, *sha256;
	unsigned long		size, cabinet_offset, cabinet_size;
	enum mh_patch_kind	kind;
};

/* Immutable source identities; unknown size/digest pairs return NULL. */
const struct mh_demo_package	*mh_demo_package_find(unsigned long size,
	const char *sha256);
const struct mh_patch_package	*mh_patch_package_find(unsigned long size,
	const char *sha256);
const struct mh_patch_package	*mh_patch_package_supported(void);

enum mh_error_code	mh_demo_package_identify(
	const struct mh_resource_source *source, const char *path,
	const struct mh_demo_package **output, struct mh_error *error_out);
enum mh_error_code	mh_retail_cabinet_verify(const struct mh_file *file,
	struct mh_error *error_out);

#endif
