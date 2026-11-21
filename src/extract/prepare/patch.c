#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/archive/unshield_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "engine/assets/content_probe.h"
#include "engine/assets/path_directories.h"
#include "engine/assets/path_installation.h"
#include "engine/core/text.h"
#include "engine/storage/digest.h"
#include "extract/output/files.h"
#include "extract/path/join.h"
#include "extract/prepare/patch.h"
#include "extract/source/installer.h"
#include "extract/source/packages.h"
#include "extract/source/patch.h"

enum {
	PATCH_PROVENANCE_SIZE = 2048
};

enum patch_component {
	PATCH_SHARED_DATA,
	PATCH_EUROPEAN_GAME,
	PATCH_STANDARD_REMAPS,
	PATCH_COMPONENT_COUNT
};

enum mh_error_code
mh_extract_patch_validate(const char *installation, struct mh_error *error_out)
{
	struct mh_content_info info;
	char *marker;
	unsigned long size;
	enum mh_error_code status;

	status = mh_content_probe(mh_filesystem_resources(), installation, NULL,
		&info, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (strcmp(info.edition->id, "retail-europe") != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"patch 3.0 requires European retail; demo and other editions are unsupported");

	if (info.revision == MH_CONTENT_REVISION_PATCH_30)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"this installation already contains patch 3.0");

	if (info.revision != MH_CONTENT_REVISION_RETAIL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unrecognized or modified retail material table; cannot qualify patch destination");

	status = mh_extract_join(
		installation, MH_ASSET_PATCH_MARKER, &marker, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_size(
		mh_filesystem_resources(), marker, &size, error_out);
	free(marker);

	if (status == MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"this installation already has a content patch");

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const char *
game_member(const char *path, unsigned int group)
{
	static const char *const data_roots[] = {MH_ASSET_CARS_PREFIX,
		MH_ASSET_DATA_PREFIX, MH_ASSET_DEMOS_PREFIX,
		MH_ASSET_SOUNDS_PREFIX, MH_ASSET_TRACKS_PREFIX};

	size_t i;

	/*
	 * These are installer component names, not target directories. All
	 * other components remain extracted in
	 * Original/patches/3.0/components/.
	 */
	if (group == PATCH_EUROPEAN_GAME &&
		strncmp(path, "Game Misc/", 10) == 0)
		return path + 10;

	if (group == PATCH_STANDARD_REMAPS &&
		strncmp(path, "DataNoDiamond/", 14) == 0)
		return path + 14;

	if (group != PATCH_SHARED_DATA ||
		strncmp(path, MH_ASSET_DATA_PREFIX,
			sizeof(MH_ASSET_DATA_PREFIX) - 1) != 0)
		return NULL;

	path += sizeof(MH_ASSET_DATA_PREFIX) - 1;

	for (i = 0; i < sizeof(data_roots) / sizeof(data_roots[0]); i++) {
		if (mh_ascii_equal_n(
			path, data_roots[i], strlen(data_roots[i])))
			return path;
	}

	return NULL;
}

static enum mh_error_code
apply_members(struct mh_archive *archive, const char *game, unsigned int group,
	mh_extract_progress progress, void *context, size_t *completed,
	size_t total, struct mh_error *error_out)
{
	const struct mh_archive_entry *entry;
	const char *relative;
	struct mh_file file;
	size_t i, count;
	enum mh_error_code status;

	count = mh_archive_count(archive);

	for (i = 0; i < count; i++) {
		entry = mh_archive_entry(archive, i);
		relative = game_member(entry->path, group);

		if (relative == NULL)
			continue;

		if (progress != NULL) {
			status = progress(context, MH_EXTRACT_PATCH, relative,
				*completed, total, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		status = mh_archive_read(
			archive, i, MH_RESOURCE_READ_LIMIT, &file, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_extract_write(game, relative, &file, error_out);
		free(file.data);

		if (status != MH_ERROR_NONE)
			return status;

		(*completed)++;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_extract_patch_apply(const char *source, const char *staging, int overlay,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	const struct mh_patch_package *supported;
	char provenance[PATCH_PROVENANCE_SIZE];
	struct mh_file cabinet, marker;
	struct mh_archive *archive;
	unsigned int group;
	size_t completed, total, i;
	enum mh_error_code status;

	completed = 0;
	total = 0;
	cabinet.data = NULL;
	archive = NULL;
	status = mh_extract_patch_read(
		source, staging, &cabinet, progress, context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_installer_components(cabinet.data,
			cabinet.size, staging, MH_ASSET_PATCH_COMPONENTS_DIR,
			progress, context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_unshield_open_components(
			cabinet.data, cabinet.size, &archive, error_out);

	if (status == MH_ERROR_NONE) {
		for (group = 0; group < PATCH_COMPONENT_COUNT; group++) {
			for (i = 0; i < mh_archive_count(archive); i++) {
				if (game_member(
					mh_archive_entry(archive, i)->path,
					group) != NULL)
					total++;
			}
		}
	}

	for (group = 0;
		group < PATCH_COMPONENT_COUNT && status == MH_ERROR_NONE;
		group++)
		status = apply_members(archive, staging, group, progress,
			context, &completed, total, error_out);

	if (status == MH_ERROR_NONE && progress != NULL)
		status = progress(context, MH_EXTRACT_PATCH, NULL, completed,
			total, error_out);

	supported = mh_patch_package_supported();
	sprintf(provenance,
		"MHCONTENTPATCH 1\nversion %s\nedition retail-europe\n"
		"%ssource %s\ncomponents Data Game_Misc DataNoDiamond\n",
		supported->version, overlay ? "kind overlay\n" : "",
		supported->sha256);
	marker.data = (unsigned char *)provenance;
	marker.size = strlen(provenance);

	if (status == MH_ERROR_NONE)
		status = mh_extract_write(
			staging, MH_ASSET_PATCH_MARKER, &marker, error_out);

	mh_archive_close(archive);
	free(cabinet.data);

	return status;
}
