#include <stdlib.h>
#include <string.h>

#include "engine/assets/packages.h"
#include "engine/assets/path_catalog.h"
#include "engine/assets/path_directories.h"
#include "engine/assets/path_installation.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/data_directory.h"

enum {
	PACKAGE_LIMIT = 128,
	PACKAGE_PATH_SIZE = 4096
};

struct package_scan {
	char		*names[PACKAGE_LIMIT];
	size_t		count;
	struct mh_error	error;
};

static enum mh_directory_action
find_track(void *context, const char *name)
{
	size_t length;

	length = strlen(name);

	if (length > sizeof(MH_ASSET_TRACK_EXTENSION) - 1 &&
		mh_ascii_equal(
			name + length - (sizeof(MH_ASSET_TRACK_EXTENSION) - 1),
			MH_ASSET_TRACK_EXTENSION))
		*(int *)context = 1;

	return MH_DIRECTORY_CONTINUE;
}

enum mh_error_code
mh_content_package_probe(const struct mh_resource_source *source,
	const char *root, struct mh_content_package_info *output,
	struct mh_error *error_out)
{
	static const char header[] =
		"MHCONTENTPATCH 1\nversion 3.0\nedition retail-europe\n";
	struct mh_content_package_info info;
	struct mh_file marker;
	char path[PACKAGE_PATH_SIZE];
	int found, overlay;
	enum mh_error_code status;

	if (source == NULL || root == NULL || *root == 0 || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid package probe");

	memset(&info, 0, sizeof(info));
	overlay = 0;
	status = mh_asset_path_find_from(source, root, MH_ASSET_PATCH_MARKER,
		path, sizeof(path), &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (found) {
		status = mh_resource_source_read_bounded(
			source, path, 2048, &marker, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (marker.size < sizeof(header) - 1 ||
			memcmp(marker.data, header, sizeof(header) - 1) != 0 ||
			memchr(marker.data, 0, marker.size) != NULL) {
			free(marker.data);
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported content patch metadata");
		}

		/*
		 * This explicit role distinguishes an overlay containing
		 * motor.exe from a complete patched bundle. Older bundle
		 * markers have no role.
		 */
		if (marker.size >=
			sizeof(header) - 1 + sizeof("kind overlay\n") - 1)
			overlay = memcmp(marker.data + sizeof(header) - 1,
					"kind overlay\n",
					sizeof("kind overlay\n") - 1) == 0;

		free(marker.data);
	}

	if (overlay) {
		/*
		 * Metadata declares the role; assets must prove compatibility.
		 */
		status = mh_content_patch_probe(
			source, root, &info.content, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		info.kind = MH_CONTENT_PACKAGE_PATCH;
	} else {
		status = mh_content_probe(
			source, root, NULL, &info.content, error_out);

		if (status == MH_ERROR_NOT_FOUND && !found) {
			/*
			 * An add-on declares its courses through ordinary
			 * definitions.
			 */
			status = mh_asset_path_from(source, root,
				MH_ASSET_GAME_DIR, path, sizeof(path),
				error_out);
			found = 0;

			if (status == MH_ERROR_NONE)
				status = mh_resource_source_list(source, path,
					find_track, &found, error_out);

			if (status == MH_ERROR_NONE && !found)
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"add-on has no track definitions");

			info.kind = MH_CONTENT_PACKAGE_ADDON;
		}

		if (status != MH_ERROR_NONE)
			return status;
	}

	*output = info;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_directory_action
collect_package(void *context, const char *name)
{
	struct package_scan *scan;
	size_t length;

	scan = context;
	length = strlen(name);

	if (length <= sizeof(MH_ASSET_PACKAGE_EXTENSION) - 1 ||
		!mh_ascii_equal(name + length -
				(sizeof(MH_ASSET_PACKAGE_EXTENSION) - 1),
			MH_ASSET_PACKAGE_EXTENSION))
		return MH_DIRECTORY_CONTINUE;

	if (scan->count == PACKAGE_LIMIT) {
		mh_error_set(&scan->error, MH_ERROR_RANGE,
			"too many asset packages");
		return MH_DIRECTORY_FAIL;
	}

	scan->names[scan->count] = malloc(length + 1);

	if (scan->names[scan->count] == NULL) {
		mh_error_set(&scan->error, MH_ERROR_MEMORY,
			"out of memory finding packages");
		return MH_DIRECTORY_FAIL;
	}

	strcpy(scan->names[scan->count++], name);

	return MH_DIRECTORY_CONTINUE;
}

static int
compare_names(const void *a, const void *b)
{
	return mh_ascii_compare(
		*(const char *const *)a, *(const char *const *)b);
}

enum mh_error_code
mh_content_mount(const struct mh_resource_source *source,
	const struct mh_resource_archive_provider *archives,
	const char *location, struct mh_vfs **output,
	struct mh_error *error_out)
{
	struct package_scan scan;
	struct mh_vfs *mounts[PACKAGE_LIMIT], *ordered[PACKAGE_LIMIT];
	struct mh_content_package_info info[PACKAGE_LIMIT];
	struct mh_data_directory *directory;
	char parent[PACKAGE_PATH_SIZE], path[PACKAGE_PATH_SIZE];
	const char *selected;
	size_t length, i, count, base, patch, bases, mount_count;
	int replace[PACKAGE_LIMIT];
	enum mh_error_code status;

	if (source == NULL || location == NULL || *location == 0 ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid content location");

	if (archives == NULL)
		return mh_vfs_open(source, NULL, location, output, error_out);

	length = strlen(location);

	if (length + 2 >= sizeof(parent))
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "asset location too long");

	strcpy(parent, location);
	selected = NULL;
	directory = NULL;
	status = mh_resource_source_open_directory(
		source, location, &directory, error_out);
	mh_data_directory_close(directory);

	if (status != MH_ERROR_NONE) {
		length = mh_path_directory_length(location);

		selected = location + length;

		if (length == 0)
			strcpy(parent, ".");
		else
			parent[length] = 0;
	}

	memset(&scan, 0, sizeof(scan));
	memset(mounts, 0, sizeof(mounts));
	status = mh_resource_source_list(
		source, parent, collect_package, &scan, error_out);

	if (scan.error.code != MH_ERROR_NONE)
		status = mh_error_forward(error_out, &scan.error);

	if (status == MH_ERROR_NONE && scan.count == 0) {
		/* Loose installations remain supported. */
		return mh_vfs_open(
			source, archives, location, output, error_out);
	}

	qsort(scan.names, scan.count, sizeof(*scan.names), compare_names);
	base = patch = PACKAGE_LIMIT;
	bases = 0;

	for (i = 0; status == MH_ERROR_NONE && i < scan.count; i++) {
		if (i != 0 &&
			mh_ascii_equal(scan.names[i - 1], scan.names[i])) {
			status = mh_error_set(error_out, MH_ERROR_EXISTS,
				"ambiguous package filenames");
			break;
		}

		if (strlen(parent) + strlen(scan.names[i]) + 2 > sizeof(path)) {
			status = mh_error_set(error_out, MH_ERROR_RANGE,
				"package path too long");
			break;
		}

		strcpy(path, parent);
		strcat(path, "/");
		strcat(path, scan.names[i]);
		status = mh_vfs_open(
			source, archives, path, &mounts[i], error_out);

		if (status == MH_ERROR_NONE)
			status = mh_content_package_probe(
				mh_vfs_resources(mounts[i]),
				mh_vfs_root(mounts[i]), &info[i], error_out);

		if (status != MH_ERROR_NONE)
			break;

		if (info[i].kind == MH_CONTENT_PACKAGE_BASE) {
			bases++;

			if (selected == NULL ||
				mh_ascii_equal(selected, scan.names[i]))
				base = i;
		} else if (info[i].kind == MH_CONTENT_PACKAGE_PATCH) {
			if (patch != PACKAGE_LIMIT)
				status = mh_error_set(error_out,
					MH_ERROR_EXISTS,
					"multiple content patches found; keep only one");

			patch = i;
		}
	}

	mount_count = scan.count;

	/*
	 * An installation directory can supply the base for sibling overlays.
	 */
	if (status == MH_ERROR_NONE && bases == 0 && selected == NULL) {
		if (mount_count == PACKAGE_LIMIT)
			status = mh_error_set(error_out, MH_ERROR_RANGE,
				"too many layers for a loose installation");
		else {
			status = mh_content_probe(source, location, NULL,
				&info[mount_count].content, error_out);

			if (status == MH_ERROR_NONE) {
				base = mount_count++;
				bases++;
				info[base].kind = MH_CONTENT_PACKAGE_BASE;
				status = mh_vfs_open(source, NULL, location,
					&mounts[base], error_out);
			} else if (status == MH_ERROR_NOT_FOUND)
				status = MH_ERROR_NONE;
		}
	}

	if (status == MH_ERROR_NONE &&
		(base == PACKAGE_LIMIT || (selected == NULL && bases != 1)))
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			bases > 1
				? "multiple base games found; select one with --assets"
				: "no complete base game found");

	if (status == MH_ERROR_NONE && patch != PACKAGE_LIMIT &&
		strcmp(info[base].content.edition->id,
			info[patch].content.edition->id) != 0)
		status = mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"patch 3.0 requires European retail assets");

	/*
	 * A self-contained patched base is sufficient. Reject a redundant
	 * overlay instead of applying it twice or concealing which files are
	 * active.
	 */
	if (status == MH_ERROR_NONE && patch != PACKAGE_LIMIT &&
		info[base].content.revision == MH_CONTENT_REVISION_PATCH_30)
		status = mh_error_set(error_out, MH_ERROR_EXISTS,
			"base game already contains patch 3.0; remove the separate patch package");

	if (status == MH_ERROR_NONE) {
		count = 0;
		ordered[count] = mounts[base];
		replace[count++] = 0;

		if (patch != PACKAGE_LIMIT) {
			ordered[count] = mounts[patch];
			replace[count++] = 1;
		}

		for (i = 0; i < scan.count; i++)
			if (info[i].kind == MH_CONTENT_PACKAGE_ADDON) {
				ordered[count] = mounts[i];
				replace[count++] = 0;
			}

		status = mh_vfs_combine(
			ordered, replace, count, output, error_out);

		if (status == MH_ERROR_NONE)
			for (i = 0; i < mount_count; i++)
				if (i == base || i == patch ||
					info[i].kind ==
						MH_CONTENT_PACKAGE_ADDON)
					mounts[i] = NULL;
	}

	for (i = 0; i < mount_count; i++)
		mh_vfs_close(mounts[i]);

	for (i = 0; i < scan.count; i++)
		free(scan.names[i]);

	return status;
}
