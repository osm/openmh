#include <stdlib.h>
#include <string.h>

#include "backends/audio/ffmpeg_encode.h"
#include "backends/audio/wave_provider.h"
#include "backends/storage/memory_resource.h"
#include "backends/video/ffmpeg_encode.h"
#include "engine/assets/path_directories.h"
#include "engine/core/text.h"
#include "extract/output/compact.h"
#include "extract/output/entry.h"

struct conversion_progress {
	mh_extract_progress	progress;
	void			*context;
};

enum mh_error_code
mh_extract_compact_available(
	const struct mh_extract_options *options, struct mh_error *error_out)
{
	if (!options->exclude_music && !mh_audio_ffmpeg_encoding_available())
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"compact music needs FFmpeg with the libopus encoder and Ogg muxer");

	if (!options->exclude_movies && !mh_video_ffmpeg_encoding_available())
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"compact movies need FFmpeg with libopus/libvpx-vp9 encoders, the WebM muxer and vp9_superframe filter");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_conversion(void *context, struct mh_error *error_out)
{
	struct conversion_progress *conversion;

	conversion = context;

	return conversion->progress == NULL
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: conversion->progress(conversion->context, MH_EXTRACT_POLL,
			NULL, 0, 0, error_out);
}

static int
is_music_wave(const char *path)
{
	size_t length;

	length = strlen(path);

	return length > sizeof(MH_ASSET_MUSIC_PREFIX) - 1 + 4 &&
		mh_ascii_equal_n(path, MH_ASSET_MUSIC_PREFIX,
			sizeof(MH_ASSET_MUSIC_PREFIX) - 1) &&
		strchr(path + sizeof(MH_ASSET_MUSIC_PREFIX) - 1, '/') == NULL &&
		mh_ascii_equal(path + length - 4, ".wav");
}

static enum mh_error_code
encode_music(const struct mh_resource_source *resources, const char *path,
	struct conversion_progress *conversion, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_audio_decoder *decoder;
	enum mh_error_code status;

	decoder = NULL;
	status = mh_audio_decoder_open(
		mh_audio_wave_decoder(), resources, path, &decoder, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_audio_ffmpeg_encode(decoder, check_conversion,
			conversion, output, error_out);

	mh_audio_decoder_close(decoder);

	return status;
}

static enum mh_error_code
add_encoded(struct mh_zip_writer *writer, const char *relative,
	const struct mh_file *file, struct mh_error *error_out)
{
	struct mh_resource_entry entry;
	const struct mh_resource_source *memory;
	enum mh_error_code status;

	entry.path = relative;
	entry.data = file->data;
	entry.size = file->size;
	memory = NULL;
	status = mh_memory_resources_open(&entry, 1, &memory, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_extract_package_add(
			writer, memory, relative, relative, 0, error_out);

	mh_memory_resources_close(memory);

	return status;
}

enum mh_error_code
mh_extract_compact_add(struct mh_zip_writer *writer,
	const struct mh_resource_source *resources, const char *path,
	const char *relative, mh_extract_progress progress, void *context,
	struct mh_error *error_out)
{
	struct conversion_progress conversion;
	struct mh_file encoded;
	char *name;
	size_t length;
	int music, movies;
	enum mh_error_code status;

	music = is_music_wave(relative);
	length = strlen(relative);
	movies = length > sizeof(MH_ASSET_MOVIES_PREFIX) - 1 + 4 &&
		mh_ascii_equal_n(relative, MH_ASSET_MOVIES_PREFIX,
			sizeof(MH_ASSET_MOVIES_PREFIX) - 1) &&
		strchr(relative + sizeof(MH_ASSET_MOVIES_PREFIX) - 1, '/') ==
			NULL &&
		mh_ascii_equal(relative + length - 4, ".pak");

	if (!music && !movies)
		return mh_extract_package_add(
			writer, resources, path, relative, 0, error_out);

	conversion.progress = progress;
	conversion.context = context;
	encoded.data = NULL;
	encoded.size = 0;
	length = strlen(relative);
	name = malloc(length + 2);

	if (name == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate compact asset name");

	strcpy(name, relative);

	if (music)
		strcpy(name + length - 3, "opus");

	status = progress == NULL ? MH_ERROR_NONE
				: progress(context, MH_EXTRACT_CONVERT,
					relative, 0, 0, error_out);

	if (status == MH_ERROR_NONE) {
		if (music)
			status = encode_music(resources, path, &conversion,
				&encoded, error_out);
		else
			status = mh_extract_compact_movies(resources, path,
				check_conversion, &conversion, &encoded,
				error_out);
	}

	if (status == MH_ERROR_NONE)
		status = add_encoded(writer, name, &encoded, error_out);

	free(encoded.data);
	free(name);

	return status;
}
