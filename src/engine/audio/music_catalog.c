#include <stdio.h>
#include <string.h>

#include "engine/assets/path_directories.h"
#include "engine/audio/music_catalog.h"
#include "engine/audio/sample_path.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "interfaces/audio/decoder.h"
#include "interfaces/storage/resource.h"

#define CD_FIRST_AUDIO_TRACK 2
#define CD_SAMPLE_RATE 44100
#define OPUS_SAMPLE_RATE 48000
#define CD_CHANNELS 2
#define CD_SAMPLE_BITS 16

static enum mh_error_code
read_duration(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *path,
	unsigned long *seconds, struct mh_error *error_out)
{
	struct mh_audio_decode_info info;
	enum mh_error_code status;

	status = mh_audio_decoder_inspect(
		decoder, source, path, &info, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if ((info.rate != CD_SAMPLE_RATE && info.rate != OPUS_SAMPLE_RATE) ||
		info.channels != CD_CHANNELS || info.bits != CD_SAMPLE_BITS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"expected music PCM: 44100 or 48000 Hz, stereo, 16 bits");

	if (info.frames / info.rate > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "CD duration exceeds range");

	*seconds = (unsigned long)(info.frames / info.rate);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_music_catalog_scan_from(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *directory, struct mh_music_catalog *catalog,
	struct mh_error *error_out)
{
	struct mh_music_catalog next;
	const char *extension;
	char default_directory[1024], name[32], path[1024];
	enum mh_error_code status;
	unsigned int track, i;

	if (source == NULL || catalog == NULL ||
		(root == NULL && directory == NULL) ||
		catalog->last_track >= MH_MUSIC_CATALOG_LAST_TRACK)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid CD catalog request");

	for (i = 0; i <= MH_MUSIC_CATALOG_LAST_TRACK; i++)
		if (catalog->seconds[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid CD catalog duration");

	if (directory == NULL) {
		if (strlen(root) > sizeof(default_directory) -
				sizeof("/" MH_ASSET_MUSIC_DIR))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"music directory path too long");

		strcpy(default_directory, root);
		strcat(default_directory, "/" MH_ASSET_MUSIC_DIR);
		directory = default_directory;
	}

	extension = mh_audio_decoder_extension(decoder);

	if (extension == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"music decoding is unavailable");

	next = *catalog;
	next.seconds[0] = next.seconds[1] = 0;

	for (track = CD_FIRST_AUDIO_TRACK; track <= MH_MUSIC_CATALOG_LAST_TRACK;
		track++) {
		sprintf(name, "track-%02u.%s", track, extension);
		status = mh_audio_sample_path_from(decoder, source, directory,
			name, path, sizeof(path), error_out);

		next.seconds[track] = 0;

		if (status == MH_ERROR_NOT_FOUND)
			break;

		if (status != MH_ERROR_NONE)
			return status;

		status = read_duration(
			decoder, source, path, &next.seconds[track], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (next.seconds[track] == 0 ||
			track == MH_MUSIC_CATALOG_LAST_TRACK)
			break;
	}

	next.last_track = track - 1;
	*catalog = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
