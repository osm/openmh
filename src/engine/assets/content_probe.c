#include <string.h>

#include "engine/assets/content_probe.h"
#include "engine/assets/executable_identity.h"
#include "engine/assets/path_installation.h"
#include "engine/assets/path_tracks.h"
#include "engine/storage/asset_path.h"
#include "engine/storage/digest.h"
#include "interfaces/storage/resource.h"

enum {
	CONTENT_PATH_SIZE = 1024
};

struct executable_edition {
	const struct mh_executable_identity	*identity;
	const char				*edition, *intro_edition;
};

/* Content policy for known executable identities, independent of offsets. */
static const struct executable_edition executables[] = {
	{&mh_executable_retail_12, "retail-europe", NULL},
	{&mh_executable_retail_30, "retail-europe", NULL},
	{&mh_executable_retail_31, "retail-europe", NULL},
	{&mh_executable_demo_glide_second, "motordemo2_3dfx_intro", NULL},
	{&mh_executable_demo_software_second, "motordemo2_soft", NULL},
	{&mh_executable_demo_glide_first, "motordemo_3dfx",
		"motordemo_3dfx_intro"},
	{&mh_executable_demo_mplayer, "motordemo_mplayer", NULL},
	{&mh_executable_demo_software_first, "motordemo_soft",
		"motordemo_soft_intro"}};

struct material_identity {
	unsigned long			size;
	const char			*digest;
	enum mh_content_revision	revision;
};

/* This marker identifies patch assets. Gameplay accepts edited tables. */
static const struct material_identity materials[] = {
	{8276UL,
		"be8b6aeb3341c2dcfffedff3f7de0263c98837fb81d53b0212c4098570e0430e",
		MH_CONTENT_REVISION_RETAIL},
	{8353UL,
		"a21f9fec3dbd180099f57041f237027ff061cd8f8ab0d4e5d0f885a6c5839837",
		MH_CONTENT_REVISION_PATCH_30}};

static enum mh_error_code
probe_executable(const struct mh_resource_source *source, const char *root,
	const struct executable_edition **output, struct mh_error *error_out)
{
	char path[CONTENT_PATH_SIZE], digest[MH_SHA256_TEXT_SIZE];
	unsigned long size;
	size_t i;
	enum mh_error_code status;

	status = mh_asset_path_from(source, root, MH_ASSET_EXECUTABLE, path,
		sizeof(path), error_out);

	if (status == MH_ERROR_NOT_FOUND)
		return mh_error_set(error_out, status,
			"game executable (" MH_ASSET_EXECUTABLE ") not found");

	if (status == MH_ERROR_NONE)
		status =
			mh_resource_source_size(source, path, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < sizeof(executables) / sizeof(executables[0]); i++)
		if (size == executables[i].identity->size)
			break;

	if (i == sizeof(executables) / sizeof(executables[0]))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unrecognized Motorhead executable in game directory");

	status = mh_resource_sha256(source, path, digest, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < sizeof(executables) / sizeof(executables[0]); i++) {
		if (size == executables[i].identity->size &&
			strcmp(digest, executables[i].identity->sha256) == 0) {
			*output = &executables[i];
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"unrecognized Motorhead executable in game directory");
}

static enum mh_error_code
probe_intro(const struct mh_resource_source *source, const char *root,
	const struct executable_edition *identity, struct mh_content_info *info,
	struct mh_error *error_out)
{
	const struct mh_content_edition *edition;
	char path[CONTENT_PATH_SIZE];
	unsigned long size;
	int found;
	enum mh_error_code status;

	info->edition = mh_content_edition_find(identity->edition);
	edition = identity->intro_edition == NULL
		? info->edition
		: mh_content_edition_find(identity->intro_edition);

	if (edition->intro.collection == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status =
		mh_asset_path_find_from(source, root, edition->intro.collection,
			path, sizeof(path), &found, error_out);

	if (status != MH_ERROR_NONE || !found)
		return status;

	status = mh_resource_source_size(source, path, &size, error_out);

	if (status == MH_ERROR_NONE && size != 0) {
		info->edition = edition;
		info->intro = edition->intro;
	}

	return status;
}

static enum mh_error_code
probe_materials(const struct mh_resource_source *source, const char *root,
	enum mh_content_revision *revision, struct mh_error *error_out)
{
	char path[CONTENT_PATH_SIZE], digest[MH_SHA256_TEXT_SIZE];
	unsigned long size;
	size_t i;
	int found;
	enum mh_error_code status;

	status = mh_asset_path_find_from(source, root, MH_ASSET_MATERIALS, path,
		sizeof(path), &found, error_out);

	if (status != MH_ERROR_NONE || !found)
		return status;

	status = mh_resource_source_size(source, path, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < sizeof(materials) / sizeof(materials[0]); i++)
		if (size == materials[i].size)
			break;

	if (i == sizeof(materials) / sizeof(materials[0]))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_resource_sha256(source, path, digest, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < sizeof(materials) / sizeof(materials[0]); i++)
		if (size == materials[i].size &&
			strcmp(digest, materials[i].digest) == 0)
			*revision = materials[i].revision;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_content_probe(const struct mh_resource_source *source, const char *root,
	const char *media_root, struct mh_content_info *output,
	struct mh_error *error_out)
{
	const struct executable_edition *identity;
	struct mh_content_info info;
	enum mh_error_code status;

	if (source == NULL || root == NULL || root[0] == 0 || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid content probe arguments");

	memset(&info, 0, sizeof(info));
	status = probe_executable(source, root, &identity, error_out);

	if (status == MH_ERROR_NONE)
		status = probe_intro(source,
			media_root == NULL ? root : media_root, identity, &info,
			error_out);

	if (status == MH_ERROR_NONE &&
		strcmp(info.edition->id, "retail-europe") == 0)
		status = probe_materials(
			source, root, &info.revision, error_out);

	if (status == MH_ERROR_NONE)
		*output = info;

	return status;
}

enum mh_error_code
mh_content_patch_probe(const struct mh_resource_source *source,
	const char *root, struct mh_content_info *output,
	struct mh_error *error_out)
{
	const struct executable_edition *identity;
	struct mh_content_info info;
	char path[CONTENT_PATH_SIZE];
	int found;
	enum mh_error_code status;

	if (source == NULL || root == NULL || root[0] == 0 || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid content patch probe arguments");

	memset(&info, 0, sizeof(info));
	status = mh_asset_path_find_from(source, root, MH_ASSET_EXECUTABLE,
		path, sizeof(path), &found, error_out);

	if (status == MH_ERROR_NONE && found) {
		status = probe_executable(source, root, &identity, error_out);

		if (status == MH_ERROR_NONE &&
			strcmp(identity->edition, "retail-europe") != 0)
			status = mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"patch executable is not European retail");
	}

	if (status == MH_ERROR_NONE)
		status = probe_materials(
			source, root, &info.revision, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (info.revision != MH_CONTENT_REVISION_PATCH_30)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"patch metadata does not match patch 3.0 material assets");

	info.edition = mh_content_edition_find("retail-europe");
	*output = info;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
