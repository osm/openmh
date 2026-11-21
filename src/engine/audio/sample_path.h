#ifndef MH_AUDIO_SAMPLE_PATH_H
#define MH_AUDIO_SAMPLE_PATH_H

#include <stddef.h>

#include "engine/core/error.h"

struct mh_audio_decoder_provider;
struct mh_resource_source;

/*
 * Resolve a logical sample reference using the selected decoder extension.
 * Only the final filename suffix changes; directories and definition records
 * retain their original names. Extensionless references receive the selected
 * suffix. Lookup keeps the existing case-insensitive resource-path rules.
 * There is no fallback to a different encoding when the selected file is
 * absent.
 *
 * Ordinary extension-aware lookup, without provider dispatch.
 */
enum mh_error_code	mh_audio_sample_file_path(
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *path, size_t capacity,
	struct mh_error *error_out);

enum mh_error_code	mh_audio_sample_path_from(
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *reference, char *path, size_t capacity,
	struct mh_error *error_out);

#endif
