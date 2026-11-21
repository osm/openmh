#include <stdlib.h>
#include <string.h>

#include "engine/assets/executable_collision.h"
#include "engine/assets/path_installation.h"
#include "engine/assets/path_tracks.h"
#include "engine/core/sha256.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

#define COLLISION_PATH_BYTES 1024

/* Resource names and embedded collision data spans. */
static const struct mh_collision_executable_layout software_second = {
	"motordemo2_soft", &mh_executable_demo_software_second, 2,
	/* Directory, filename, file offset and byte count. */
	{{MH_ASSET_GOLDBRIDGE_OBJECTS_DIR, MH_ASSET_GOLDBRIDGE_COLLISION_NAME,
		0xbf90c, 0x403b8},
		{MH_ASSET_ATLANTIKA_OBJECTS_DIR,
			MH_ASSET_ATLANTIKA_COLLISION_NAME, 0x679f0, 0x4f9e8}}};

static const struct mh_collision_executable_layout glide_second = {
	"motordemo2_3dfx_intro", &mh_executable_demo_glide_second, 2,
	{{MH_ASSET_GOLDBRIDGE_OBJECTS_DIR, MH_ASSET_GOLDBRIDGE_COLLISION_NAME,
		0xba43c, 0x403b8},
		{MH_ASSET_ATLANTIKA_OBJECTS_DIR,
			MH_ASSET_ATLANTIKA_COLLISION_NAME, 0x62830, 0x4f9e8}}};

static const struct mh_collision_executable_layout mplayer = {
	"motordemo_mplayer", &mh_executable_demo_mplayer, 1,
	{{MH_ASSET_NEOCITY_OBJECTS_DIR, MH_ASSET_NEOCITY_COLLISION_NAME,
		0xc790c, 0x40648},
		{NULL, NULL, 0, 0}}};

static const struct mh_collision_executable_layout *const layouts[] = {
	&software_second, &glide_second, &mplayer};

const struct mh_collision_executable_layout *
mh_collision_executable_layout(const char *edition)
{
	size_t i;

	if (edition == NULL)
		return NULL;

	for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++)
		if (strcmp(edition, layouts[i]->edition) == 0)
			return layouts[i];

	return NULL;
}

enum mh_error_code
mh_collision_executable_open(const struct mh_resource_source *source,
	const char *root, const struct mh_collision_executable_layout *layout,
	struct mh_file *output, struct mh_error *error_out)
{
	struct mh_sha256 hash;
	struct mh_file file;
	unsigned char digest[MH_SHA256_BYTES];
	char path[COLLISION_PATH_BYTES], text[MH_SHA256_TEXT_SIZE];
	enum mh_error_code status;

	status = mh_asset_path_from(source, root, MH_ASSET_EXECUTABLE, path,
		sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read_bounded(
		source, path, layout->identity->size, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_sha256_init(&hash);
	mh_sha256_update(&hash, file.data, file.size);
	mh_sha256_finish(&hash, digest);
	mh_sha256_text(digest, text);

	if (file.size != layout->identity->size ||
		strcmp(text, layout->identity->sha256) != 0) {
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"embedded collision requires a recognized original executable");
	}

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const struct mh_collision_executable_file *
find_reference(const struct mh_collision_executable_layout *layout,
	const char *reference)
{
	const struct mh_collision_executable_file *file;
	char path[COLLISION_PATH_BYTES];
	size_t i, length;

	length = strlen(reference);

	if (length >= sizeof(path))
		return NULL;

	for (i = 0; i <= length; i++)
		path[i] = reference[i] == '\\' ? '/' : reference[i];

	for (i = 0; i < layout->count; i++) {
		file = &layout->files[i];
		length = strlen(file->directory);

		if (mh_ascii_equal_n(path, file->directory, length) &&
			path[length] == '/' &&
			mh_ascii_equal(path + length + 1, file->name))
			return file;
	}

	return NULL;
}

enum mh_error_code
mh_collision_executable_read(const struct mh_resource_source *source,
	const char *root, const char *reference, struct mh_file *output,
	struct mh_error *error_out)
{
	const struct mh_collision_executable_layout *layout;
	const struct mh_collision_executable_file *part;
	struct mh_file executable, file;
	char path[COLLISION_PATH_BYTES];
	unsigned long size;
	size_t i;
	enum mh_error_code status;

	/* Ordinary missing car files must not trigger an executable read. */
	for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++)
		if (find_reference(layouts[i], reference) != NULL)
			break;

	if (i == sizeof(layouts) / sizeof(layouts[0]))
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"no embedded collision for this reference");

	status = mh_asset_path_from(source, root, MH_ASSET_EXECUTABLE, path,
		sizeof(path), error_out);

	if (status == MH_ERROR_NONE)
		status =
			mh_resource_source_size(source, path, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
		layout = layouts[i];
		part = find_reference(layout, reference);

		if (size != layout->identity->size || part == NULL)
			continue;

		status = mh_collision_executable_open(
			source, root, layout, &executable, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (part->offset > executable.size ||
			part->size > executable.size - part->offset) {
			free(executable.data);
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated embedded collision");
		}

		file.size = part->size;
		file.data = malloc(file.size);

		if (file.data != NULL)
			memcpy(file.data, executable.data + part->offset,
				file.size);

		free(executable.data);

		if (file.data == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory reading embedded collision");

		*output = file;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"no embedded collision layout for this executable");
}
