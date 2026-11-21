#ifndef MH_CONTENT_PACKAGES_H
#define MH_CONTENT_PACKAGES_H

#include "engine/assets/content_probe.h"
#include "engine/storage/vfs.h"

#define MH_ASSET_PACKAGE_EXTENSION ".ass"

enum mh_content_package_kind {
	MH_CONTENT_PACKAGE_BASE,
	MH_CONTENT_PACKAGE_PATCH,
	MH_CONTENT_PACKAGE_ADDON
};

struct mh_content_package_info {
	enum mh_content_package_kind	kind;
	struct mh_content_info		content;
};

/* Classify by contents. Complete patched bundles remain base packages. */
enum mh_error_code	mh_content_package_probe(
	const struct mh_resource_source *source, const char *root,
	struct mh_content_package_info *output, struct mh_error *error_out);

/*
 * Scan a directory, or select an explicit base and scan its siblings. Exactly
 * one base is required without explicit selection. A loose installation
 * supplies the base when its directory contains only overlays. No filename
 * implies order.
 */
enum mh_error_code	mh_content_mount(
	const struct mh_resource_source *source,
	const struct mh_resource_archive_provider *archives,
	const char *location, struct mh_vfs **output,
	struct mh_error *error_out);

#endif
