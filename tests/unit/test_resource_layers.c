#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "engine/assets/packages.h"
#include "engine/storage/overlay.h"
#include "interfaces/storage/data_directory.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "layers:%d: %s\n", __LINE__, #x);      \
			return 1;                                              \
		}                                                              \
	} while (0)

static enum mh_directory_action
count_name(void *context, const char *name)
{
	(void)name;
	(*(unsigned int *)context)++;

	return MH_DIRECTORY_CONTINUE;
}

int
main(void)
{
	static const unsigned char base[] = "original", patch[] = "patched",
				addon[] = "new";
	static const unsigned char marker[] =
		"MHCONTENTPATCH 1\nversion 3.0\nedition retail-europe\nkind overlay\n";
	struct mh_resource_entry entries[] = {
		{"base/Data/shared", base, sizeof(base) - 1},
		{"base/Data/retained", base, sizeof(base) - 1},
		{"patch/data/SHARED", patch, sizeof(patch) - 1},
		{"patch/patch.txt", marker, sizeof(marker) - 1},
		{"addon/Game/coast.trk", addon, sizeof(addon) - 1},
		{"addon/Tracks/coast/world", addon, sizeof(addon) - 1},
		{"collision/DATA/shared", addon, sizeof(addon) - 1},
		{"directory/Data/shared/child", addon, sizeof(addon) - 1}};

	const struct mh_resource_source *source, *view;
	struct mh_resource_layer layers[3];
	struct mh_resource_overlay *overlay, *unchanged;
	struct mh_content_package_info info, previous;
	struct mh_data_directory *directory;
	struct mh_data_file *token;
	struct mh_file file;
	unsigned long size;
	unsigned int count;
	char bytes[3];

	CHECK(mh_memory_resources_open(entries,
		sizeof(entries) / sizeof(entries[0]), &source,
		NULL) == MH_ERROR_NONE);
	layers[0].source = layers[1].source = layers[2].source = source;
	layers[0].root = "base";
	layers[1].root = "patch";
	layers[2].root = "addon";
	layers[0].replace = layers[2].replace = 0;
	layers[1].replace = 1;
	CHECK(mh_resource_overlay_open(layers, 3, "game", &overlay, NULL) ==
		MH_ERROR_NONE);
	view = mh_resource_overlay_source(overlay);
	CHECK(mh_resource_source_read(view, "game/data/SHARED", &file, NULL) ==
		MH_ERROR_NONE);
	CHECK(file.size == 7 && memcmp(file.data, patch, 7) == 0);
	free(file.data);
	CHECK(mh_resource_source_size(view, "game/Data/shared", &size, NULL) ==
			MH_ERROR_NONE &&
		size == 7);
	CHECK(mh_resource_source_read_at(view, "game/Data/shared", 1, bytes, 3,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(bytes, "atc", 3) == 0);
	CHECK(mh_resource_source_read_bounded(view, "game/Data/shared", 3,
		&file, NULL) == MH_ERROR_RANGE);
	CHECK(mh_resource_source_read_at(view, "game/Data/shared", 6, bytes, 3,
		NULL) == MH_ERROR_RANGE);
	count = 0;
	CHECK(mh_resource_source_list(view, "game/data", count_name, &count,
		NULL) == MH_ERROR_NONE &&
		count == 2);
	CHECK(mh_resource_source_open_directory(
		view, "game/Data", &directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_read(directory, "shared", 20, &file, &token,
		NULL) == MH_ERROR_NONE);
	CHECK(file.size == 7 && memcmp(file.data, patch, 7) == 0);
	free(file.data);
	CHECK(mh_data_file_validate(token, NULL) == MH_ERROR_NONE);
	mh_data_file_close(token);
	mh_data_directory_close(directory);
	CHECK(mh_resource_source_size(view, "game/Tracks/coast/world", &size,
		NULL) == MH_ERROR_NONE &&
		size == 3);
	size = 123;
	CHECK(mh_resource_source_size(view, "elsewhere/Data/shared", &size,
		NULL) == MH_ERROR_NOT_FOUND &&
		size == 123);
	CHECK(mh_resource_source_size(view, "game/Data/missing", &size, NULL) ==
			MH_ERROR_NOT_FOUND &&
		size == 123);
	unchanged = overlay;
	layers[2].root = "collision";
	CHECK(mh_resource_overlay_open(layers, 3, "game", &unchanged, NULL) ==
			MH_ERROR_EXISTS &&
		unchanged == overlay);
	layers[2].root = "directory";
	layers[2].replace = 1;
	CHECK(mh_resource_overlay_open(layers, 3, "game", &unchanged, NULL) ==
			MH_ERROR_EXISTS &&
		unchanged == overlay);
	mh_resource_overlay_close(overlay);

	/* Removing the patch exposes exactly the original bytes. */
	CHECK(mh_resource_overlay_open(layers, 1, "game", &overlay, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_resource_source_read(mh_resource_overlay_source(overlay),
		"game/Data/shared", &file, NULL) == MH_ERROR_NONE);
	CHECK(file.size == 8 && memcmp(file.data, base, 8) == 0);
	free(file.data);
	mh_resource_overlay_close(overlay);
	CHECK(mh_content_package_probe(source, "addon", &info, NULL) ==
			MH_ERROR_NONE &&
		info.kind == MH_CONTENT_PACKAGE_ADDON);
	previous = info;

	/* A role marker alone does not qualify a content patch. */
	CHECK(mh_content_package_probe(source, "patch", &info, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&info, &previous, sizeof(info)) == 0);
	CHECK(mh_content_package_probe(source, "base", &info, NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(memcmp(&info, &previous, sizeof(info)) == 0);
	mh_memory_resources_close(source);
	puts("Resource overlays: precedence, merged directories, range reads, conflicts, removal and roles passed");

	return 0;
}
