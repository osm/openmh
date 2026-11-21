#ifndef MH_ASSETS_CONTENT_PROBE_H
#define MH_ASSETS_CONTENT_PROBE_H

#include "engine/assets/content_edition.h"
#include "engine/core/error.h"

struct mh_resource_source;

enum mh_content_revision {
	MH_CONTENT_REVISION_UNKNOWN,
	MH_CONTENT_REVISION_RETAIL,
	MH_CONTENT_REVISION_PATCH_30
};

struct mh_content_info {
	const struct mh_content_edition	*edition;
	struct mh_content_intro		intro;

	/* Recognized retail material table, not a whole-installation audit. */
	enum mh_content_revision	revision;
};

/*
 * Identify a supported executable, then probe optional video and retail patch
 * assets through the prepared resource view. Missing/unknown executables fail;
 * modified assets do not change the edition. NULL media_root uses root for
 * video lookup; an explicit media root supports separately installed videos.
 * Outputs are unchanged on failure.
 * This selects content policy; individual loaders still validate their assets.
 */
enum mh_error_code	mh_content_probe(
	const struct mh_resource_source *source, const char *root,
	const char *media_root, struct mh_content_info *output,
	struct mh_error *error_out);

/*
 * Qualify a retail 3.0 overlay by its material table. An executable is optional
 * because data-only patches inherit the base executable; if supplied it must
 * identify the same edition. Outputs are unchanged on failure.
 */
enum mh_error_code	mh_content_patch_probe(
	const struct mh_resource_source *source, const char *root,
	struct mh_content_info *output, struct mh_error *error_out);

#endif
