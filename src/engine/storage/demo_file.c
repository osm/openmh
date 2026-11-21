#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/text.h"
#include "engine/storage/demo_file.h"
#include "engine/storage/path_names.h"
#include "interfaces/storage/data_stream.h"

#define INITIAL_FRAMES 64
#define SAVE_NAME_LIMIT 23
#define SAVE_FILENAME_SIZE (SAVE_NAME_LIMIT + 5)

struct mh_demo_file {
	struct mh_data_directory	*directory;
	struct mh_data_stream		*stream;
	struct mh_data_file		*target;
	char				*filename;
	char				target_name[SAVE_FILENAME_SIZE];
	size_t				frame_count, frame_bytes;
	size_t				header_bytes, metadata_bytes;
	unsigned char			header_metadata[MH_DEMO_MAX_HEADER_BYTES];
	int	ignore_case, target_probed, sealed, published, write_failed, failed;
};

struct name_lookup {
	const char	*name;
	char		*matched;
	unsigned int	count;
	int		ignore_case;
};

static enum mh_directory_action
match_name(void *context, const char *name)
{
	struct name_lookup *lookup;

	lookup = context;

	if (lookup->ignore_case ? !mh_ascii_equal(name, lookup->name)
				: strcmp(name, lookup->name) != 0)
		return MH_DIRECTORY_CONTINUE;

	lookup->count++;

	if (lookup->matched != NULL)
		strcpy(lookup->matched, name);

	return lookup->count > 1 ? MH_DIRECTORY_STOP : MH_DIRECTORY_CONTINUE;
}

static enum mh_error_code
find_name(struct mh_demo_file *file, const char *name, int ignore_case,
	char *matched, unsigned int *count, struct mh_error *error_out)
{
	struct name_lookup lookup;
	enum mh_error_code status;

	lookup.name = name;
	lookup.matched = matched;
	lookup.count = 0;
	lookup.ignore_case = ignore_case;
	status = mh_data_directory_list(
		file->directory, match_name, &lookup, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*count = lookup.count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_unused(void *context, struct mh_data_directory *directory,
	struct mh_error *error_out)
{
	struct mh_demo_file *file;
	unsigned int count;
	enum mh_error_code status;

	(void)directory;
	file = context;
	status = find_name(file, file->filename, file->ignore_case, NULL,
		&count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (count != 0)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"recording destination already exists");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_demo_file_write_failed(const struct mh_demo_file *file)
{
	return file != NULL && file->write_failed;
}

void
mh_demo_file_abort(struct mh_demo_file *file)
{
	if (file == NULL)
		return;

	mh_data_file_close(file->target);
	mh_data_stream_close(file->stream);
	mh_data_directory_close(file->directory);
	free(file->filename);
	free(file);
}

static void
copy_header_metadata(struct mh_demo_file *file, const struct mh_demo *header,
	const unsigned char *bytes)
{
	file->frame_bytes = MH_DEMO_FRAME_HEADER_BYTES +
		MH_DEMO_VEHICLE_BYTES * header->player_count;

	file->header_bytes = mh_demo_header_size(header->version);
	file->metadata_bytes = mh_demo_crossings_offset(header->version);
	memcpy(file->header_metadata, bytes, file->metadata_bytes);
}

enum mh_error_code
mh_demo_file_open_in(struct mh_data_directory *directory, const char *filename,
	int ignore_case, const struct mh_demo *header,
	struct mh_demo_file **output, struct mh_error *error_out)
{
	struct mh_demo_file *file;
	unsigned char bytes[MH_DEMO_MAX_HEADER_BYTES];
	enum mh_error_code status;

	if (directory == NULL || filename == NULL || filename[0] == 0 ||
		strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0 ||
		strpbrk(filename, "/\\:") != NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording output name or directory");

	status = mh_demo_header_write(header, bytes, sizeof(bytes), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	file = calloc(1, sizeof(*file));

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening recording");

	file->filename = malloc(strlen(filename) + 1);

	if (file->filename == NULL) {
		free(file);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening recording");
	}

	strcpy(file->filename, filename);
	file->directory = directory;
	file->ignore_case = ignore_case;
	status = check_unused(file, directory, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_stream_open(
			directory, &file->stream, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_stream_append(file->stream, bytes,
			mh_demo_header_size(header->version), error_out);

	if (status != MH_ERROR_NONE) {
		if (file->stream != NULL)
			status = mh_error_set(error_out, status,
				"cannot write recording header");
		else
			status = mh_error_set(error_out, status,
				status == MH_ERROR_EXISTS
					? "recording destination already exists"
					: "cannot open recording file");

		mh_data_stream_close(file->stream);
		free(file->filename);
		free(file);

		return status;
	}

	copy_header_metadata(file, header, bytes);
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_file_restart(struct mh_demo_file *file, const struct mh_demo *header,
	struct mh_error *error_out)
{
	struct mh_data_stream *stream;
	unsigned char bytes[MH_DEMO_MAX_HEADER_BYTES];
	enum mh_error_code status;

	if (file == NULL || file->sealed || file->failed || file->published)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"recording stream cannot restart");

	status = mh_demo_header_write(header, bytes, sizeof(bytes), error_out);

	if (status == MH_ERROR_NONE)
		status = check_unused(file, file->directory, error_out);

	stream = NULL;

	if (status == MH_ERROR_NONE)
		status = mh_data_stream_open(
			file->directory, &stream, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_stream_append(stream, bytes,
			mh_demo_header_size(header->version), error_out);

	if (status != MH_ERROR_NONE) {
		if (stream != NULL)
			status = mh_error_set(error_out, status,
				"cannot write recording header");

		mh_data_stream_close(stream);

		return status;
	}

	mh_data_stream_close(file->stream);
	file->stream = stream;
	file->frame_count = 0;
	copy_header_metadata(file, header, bytes);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_file_append(struct mh_demo_file *file,
	const struct mh_demo_record_event *event, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (file == NULL || event == NULL || file->sealed || file->failed)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"recording stream is unavailable");

	if (event->size == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (event->size != MH_DEMO_BLOCK_FRAMES * file->frame_bytes ||
		event->size > sizeof(event->data) ||
		file->frame_count > ((size_t)LONG_MAX - file->header_bytes) /
					file->frame_bytes -
				MH_DEMO_BLOCK_FRAMES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording output block or file size");

	status = mh_data_stream_append(
		file->stream, event->data, event->size, error_out);

	if (status != MH_ERROR_NONE) {
		file->failed = 1;
		file->write_failed = 1;
		return mh_error_set(
			error_out, status, "cannot append recording block");
	}

	file->frame_count += MH_DEMO_BLOCK_FRAMES;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_file_seal(struct mh_demo_file *file, const struct mh_demo *header,
	size_t frame_count, struct mh_error *error_out)
{
	unsigned char bytes[MH_DEMO_MAX_HEADER_BYTES];
	enum mh_error_code status;

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing recording stream");

	status = mh_demo_header_write(header, bytes, sizeof(bytes), error_out);

	if (status == MH_ERROR_NONE && (file->failed || file->sealed))
		status = mh_error_set(
			error_out, MH_ERROR_IO, "recording stream failed");

	if (status == MH_ERROR_NONE &&
		(frame_count < INITIAL_FRAMES ||
			frame_count != file->frame_count))
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"recording has fewer than 64 frames or incomplete blocks");

	if (status == MH_ERROR_NONE &&
		(file->header_bytes != mh_demo_header_size(header->version) ||
			memcmp(file->header_metadata, bytes,
				file->metadata_bytes) != 0))
		status = mh_error_set(error_out, MH_ERROR_STATE,
			"recording metadata changed before finalization");

	if (status == MH_ERROR_NONE &&
		mh_data_stream_size(file->stream) !=
			file->header_bytes +
				file->frame_count * file->frame_bytes)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"recording file length differs from its frame count");

	if (status == MH_ERROR_NONE) {
		status = mh_data_stream_write_at(file->stream,
			file->metadata_bytes, bytes + file->metadata_bytes,
			file->header_bytes - file->metadata_bytes, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_data_stream_seal(file->stream, error_out);

		if (status != MH_ERROR_NONE)
			status = mh_error_set(error_out, status,
				"cannot finalize recording file");
	}

	file->sealed = status == MH_ERROR_NONE;
	file->failed = status != MH_ERROR_NONE;

	return status;
}

enum mh_error_code
mh_demo_file_publish(struct mh_demo_file *file, struct mh_error *error_out)
{
	struct mh_data_file *created;
	enum mh_error_code status;

	if (file == NULL || !file->sealed || file->published)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"recording is not ready for publication");

	status = mh_data_stream_create(file->stream, file->filename,
		check_unused, file, &created, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_data_file_close(created);
	file->published = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_file_finish(struct mh_demo_file *file, const struct mh_demo *header,
	size_t frame_count, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_demo_file_seal(file, header, frame_count, error_out);

	if (status == MH_ERROR_NONE) {
		status = mh_demo_file_publish(file, error_out);

		/*
		 * Provider diagnostics may not survive consumption of this
		 * owner.
		 */
		if (status != MH_ERROR_NONE)
			status = mh_error_set(error_out, status,
				"cannot publish recording without replacing an existing file");
	}

	mh_demo_file_abort(file);

	return status;
}

static enum mh_error_code
build_save_filename(const unsigned char *name, char output[SAVE_FILENAME_SIZE],
	struct mh_error *error_out)
{
	size_t length, i;

	length = 0;

	while (length <= SAVE_NAME_LIMIT && name[length] != 0)
		length++;

	if (length == 0 || length > SAVE_NAME_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"Enter a name of 1 to 23 characters.");

	for (i = 0; i < length; i++)
		if (name[i] < 32 || strchr("/\\:\"<>|?*", name[i]) != NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"The recording name contains an invalid character.");

	if (name[length - 1] == '.' || name[length - 1] == ' ')
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"The recording name cannot end with a dot or space.");

	memcpy(output, name, length);
	strcpy(output + length, MH_RECORDING_EXTENSION);
	mh_ascii_lower(output);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
find_target(struct mh_demo_file *file, const char *name,
	char matched[SAVE_FILENAME_SIZE], int *exists,
	struct mh_error *error_out)
{
	unsigned int count;
	enum mh_error_code status;

	strcpy(matched, name);
	status = find_name(file, name, 1, matched, &count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (count > 1)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"Several recordings share this name. Choose another.");

	*exists = count != 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_file_probe(struct mh_demo_file *file, const char *asset_root,
	const unsigned char *name, int *exists, struct mh_error *error_out)
{
	char filename[SAVE_FILENAME_SIZE], matched[SAVE_FILENAME_SIZE];
	struct mh_data_file *target;
	enum mh_error_code status;
	int found;

	found = 0;

	if (file != NULL) {
		file->target_probed = 0;
		mh_data_file_close(file->target);
		file->target = NULL;
	}

	if (file == NULL || !file->sealed || file->published ||
		asset_root == NULL || name == NULL || exists == NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"Recording is not ready to save.");

	status = build_save_filename(name, filename, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_data_directory_protect(
		file->directory, asset_root, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = find_target(file, filename, matched, &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	target = NULL;

	if (found)
		status = mh_data_directory_open_file(
			file->directory, matched, &target, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	strcpy(file->target_name, matched);
	file->target = target;
	file->target_probed = 1;
	file->ignore_case = 1;
	*exists = found;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_target(void *context, struct mh_data_directory *directory,
	struct mh_error *error_out)
{
	struct mh_demo_file *file;
	char matched[SAVE_FILENAME_SIZE];
	enum mh_error_code status;
	int found;

	found = 0;
	(void)directory;
	file = context;
	status = find_target(
		file, file->target_name, matched, &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (found != (file->target != NULL) ||
		strcmp(matched, file->target_name) != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"Recording changed. Please enter its name again.");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_file_save(
	struct mh_demo_file *file, int overwrite, struct mh_error *error_out)
{
	struct mh_data_file *created;
	enum mh_error_code status;

	if (file == NULL || !file->sealed || file->published ||
		!file->target_probed)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"Choose a recording name before saving.");

	if (overwrite != (file->target != NULL))
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"Existing recording requires overwrite confirmation.");

	if (file->target != NULL) {
		status = mh_data_file_refresh(file->target, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_data_stream_replace(file->stream,
				file->target, check_target, file, error_out);
	} else {
		status = mh_data_stream_create(file->stream, file->target_name,
			check_target, file, &created, error_out);

		if (status == MH_ERROR_NONE)
			mh_data_file_close(created);
	}

	if (status == MH_ERROR_NONE)
		file->published = 1;

	return status;
}
