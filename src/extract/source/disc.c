#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/archive/unshield_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "engine/archive/iso.h"
#include "engine/assets/path_directories.h"
#include "engine/assets/path_installation.h"
#include "engine/assets/path_movies.h"
#include "engine/assets/prepare_cd_audio.h"
#include "engine/assets/prepare_disc_layout.h"
#include "engine/storage/asset_path.h"
#include "extract/output/files.h"
#include "extract/path/join.h"
#include "extract/source/disc.h"
#include "extract/source/disc_internal.h"
#include "extract/source/installer.h"
#include "extract/source/packages.h"

struct retail_source {
	struct mh_archive	*image;
	const char		*directory;
};

static enum mh_error_code
track_path(const char *layout_path, const char *name, char **output,
	struct mh_error *error_out)
{
	return mh_extract_join_prefix(layout_path,
		mh_path_directory_length(layout_path), name, output, error_out);
}

static enum mh_error_code
open_layout_image(const char *path, const struct mh_disc_layout *layout,
	struct mh_archive **output, struct mh_error *error_out)
{
	char *data_path;
	unsigned long size;
	enum mh_error_code status;

	status =
		track_path(path, layout->tracks[0].file, &data_path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (layout->image_size != 0) {
		status = mh_resource_source_size(
			mh_filesystem_resources(), data_path, &size, error_out);

		if (status == MH_ERROR_NONE && size != layout->image_size)
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"cdrdao image size does not match its track layout");
	}

	if (status == MH_ERROR_NONE)
		status = mh_iso_open_track(mh_filesystem_resources(), data_path,
			layout->data_format, layout->tracks[0].length, output,
			error_out);

	free(data_path);

	return status;
}

static enum mh_error_code
open_image(const char *path, struct mh_disc_layout *layout, int *has_audio,
	struct mh_archive **output, struct mh_error *error_out)
{
	struct mh_file file;
	unsigned long size;
	enum mh_error_code status;

	status = mh_resource_source_size(
		mh_filesystem_resources(), path, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*has_audio = size <= MH_DISC_LAYOUT_TEXT_LIMIT;

	if (!*has_audio)
		return mh_iso_open(mh_filesystem_resources(), path, MH_DISC_ISO,
			output, error_out);

	status = mh_resource_source_read_bounded(mh_filesystem_resources(),
		path, MH_DISC_LAYOUT_TEXT_LIMIT, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_disc_layout_parse(file.data, file.size, layout, error_out);
	free(file.data);

	if (status != MH_ERROR_NONE)
		return status;

	return open_layout_image(path, layout, output, error_out);
}

static enum mh_error_code
read_disc_file(const char *directory, const char *name, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	char *path;
	size_t capacity;
	enum mh_error_code status;

	status = mh_extract_join(directory, name, &path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	capacity = strlen(path) + 1;
	status = mh_asset_path_from(mh_filesystem_resources(), directory, name,
		path, capacity, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_read_bounded(
			mh_filesystem_resources(), path, limit, output,
			error_out);

	free(path);

	return status;
}

static enum mh_error_code
extract_game(const char *source, const char *staging,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	struct mh_file file;
	struct mh_archive *cabinet;
	enum mh_error_code status;

	status = read_disc_file(source, "MOTOR/DISK1/DATA1.CAB",
		MH_RETAIL_CABINET_BYTES, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	cabinet = NULL;
	status = mh_retail_cabinet_verify(&file, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_installer_components(file.data, file.size,
			staging, MH_ASSET_RETAIL_COMPONENTS_DIR, progress,
			context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_unshield_open(
			file.data, file.size, &cabinet, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_unpack(cabinet, staging, MH_EXTRACT_UNPACK,
			progress, context, error_out);

	mh_archive_close(cabinet);
	free(file.data);

	return status;
}

static enum mh_error_code
extract_video(
	const char *source, const char *staging, struct mh_error *error_out)
{
	struct mh_file file;
	char *directory, *path;
	enum mh_error_code status;

	status = read_disc_file(source, MH_ASSET_DISC_MOVIES,
		MH_RESOURCE_READ_LIMIT, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	directory = NULL;
	path = NULL;
	status = mh_extract_join(
		staging, MH_ASSET_MOVIES_DIR, &directory, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_user_data_create_directory(
			mh_filesystem_user_data(), directory, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_join(directory, MH_ASSET_RETAIL_MOVIES_NAME,
			&path, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_user_data_replace(mh_filesystem_user_data(), path,
			file.data, file.size, error_out);

	free(path);
	free(directory);
	free(file.data);

	return status;
}

enum mh_error_code
mh_extract_disc_audio(const struct mh_resource_source *resources,
	const char *layout_path, const struct mh_disc_layout *layout,
	const char *staging, mh_extract_progress progress, void *context,
	struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	char *root, *path;
	unsigned int i;
	char track[16];
	enum mh_error_code status;

	directory = NULL;
	root = NULL;
	status = mh_extract_join(staging, MH_ASSET_MUSIC_DIR, &root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_user_data_create_directory(
			mh_filesystem_user_data(), root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_filesystem_data_directory_open(
			root, NULL, &directory, error_out);

	for (i = 1; i < MH_RETAIL_DISC_TRACKS && status == MH_ERROR_NONE; i++) {
		path = NULL;

		sprintf(track, "%02u", i + 1);

		if (progress != NULL)
			status = progress(context, MH_EXTRACT_AUDIO, track, 0,
				1, error_out);

		if (status == MH_ERROR_NONE)
			status = track_path(layout_path, layout->tracks[i].file,
				&path, error_out);

		if (status == MH_ERROR_NONE) {
			status = mh_cd_audio_extract_track(resources, path,
				layout->tracks[i].offset,
				layout->tracks[i].length,
				layout->big_endian_audio, directory, i + 1,
				error_out);
		}

		if (status == MH_ERROR_NONE && progress != NULL)
			status = progress(context, MH_EXTRACT_AUDIO, track, 1,
				1, error_out);

		free(path);
	}

	mh_data_directory_close(directory);
	free(root);

	return status;
}

static enum mh_error_code
extract_originals(const struct retail_source *source, const char *staging,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	char *root;
	enum mh_error_code status;

	status = mh_extract_join(staging, MH_ASSET_DISC_DIR, &root, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (source->image != NULL)
		status = mh_extract_unpack(source->image, root,
			MH_EXTRACT_READ_DISC, progress, context, error_out);
	else
		status = mh_extract_copy(
			source->directory, root, progress, context, error_out);
	free(root);

	return status;
}

static enum mh_error_code
extract_data(const struct retail_source *source, const char *staging,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	char *root;
	enum mh_error_code status;

	status = extract_originals(
		source, staging, progress, context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Reuse preserved files instead of reading a physical CD twice. */
	status = mh_extract_join(staging, MH_ASSET_DISC_DIR, &root, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = extract_game(root, staging, progress, context, error_out);

	if (status == MH_ERROR_NONE && progress != NULL)
		status = progress(context, MH_EXTRACT_UNPACK,
			MH_ASSET_DISC_MOVIES, 0, 1, error_out);

	if (status == MH_ERROR_NONE)
		status = extract_video(root, staging, error_out);

	if (status == MH_ERROR_NONE && progress != NULL)
		status = progress(
			context, MH_EXTRACT_MOVIES, NULL, 1, 1, error_out);

	free(root);

	return status;
}

enum mh_error_code
mh_extract_disc_archive(struct mh_archive *archive, const char *staging,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	struct retail_source source;

	source.image = archive;
	source.directory = NULL;

	return extract_data(&source, staging, progress, context, error_out);
}

enum mh_error_code
mh_extract_disc_files(const char *directory, const char *staging,
	mh_extract_progress progress, void *context, struct mh_error *error_out)
{
	struct retail_source source;
	enum mh_error_code status;

	source.image = NULL;
	source.directory = directory;
	status = extract_data(&source, staging, progress, context, error_out);

	return status;
}

enum mh_error_code
mh_extract_disc_unpack(const char *source, const char *staging,
	int *has_audio_out, mh_extract_progress progress, void *context,
	struct mh_error *error_out)
{
	struct retail_source retail;
	struct mh_disc_layout layout;
	int has_audio;
	enum mh_error_code status;

	retail.image = NULL;
	retail.directory = NULL;
	status = open_image(
		source, &layout, &has_audio, &retail.image, error_out);

	if (status == MH_ERROR_NONE)
		status = extract_data(
			&retail, staging, progress, context, error_out);

	if (status == MH_ERROR_NONE && has_audio)
		status = mh_extract_disc_audio(mh_filesystem_resources(),
			source, &layout, staging, progress, context, error_out);

	if (status == MH_ERROR_NONE)
		*has_audio_out = has_audio;

	mh_archive_close(retail.image);

	return status;
}
