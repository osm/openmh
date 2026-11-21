#ifndef MH_VIDEO_SOURCE_H
#define MH_VIDEO_SOURCE_H

#include <stddef.h>

#include "engine/core/error.h"
#include "engine/core/file.h"

struct mh_resource_source;
struct mh_video_source_provider;

/*
 * Resolve a logical collection reference relative to a content root. The
 * result is a provider-defined selection string, not necessarily a filename.
 * Existing content uses legacy archive names as keys; other providers may map
 * those keys to standalone clips or another container. Missing collections set
 * found to zero. Output is unchanged on failure or absence.
 */
enum mh_error_code	mh_video_source_resolve(
	const struct mh_video_source_provider *provider,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *output, size_t capacity, int *found,
	struct mh_error *error_out);

/*
 * Read one encoded clip. A collection request selects its numbered member;
 * otherwise reference directly identifies one clip and index is ignored.
 * Success transfers malloc-owned bytes, freed by the caller. Failure preserves
 * output. All inputs are borrowed only for the operation; no decoder is opened.
 */
enum mh_error_code	mh_video_source_read(
	const struct mh_video_source_provider *provider,
	const struct mh_resource_source *resources, const char *reference,
	int collection, unsigned int index, struct mh_file *output,
	struct mh_error *error_out);

#endif
