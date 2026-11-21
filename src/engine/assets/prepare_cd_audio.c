#include <stdio.h>
#include <string.h>

#include "engine/archive/disc.h"
#include "engine/assets/path_audio.h"
#include "engine/assets/prepare_cd_audio.h"
#include "engine/assets/wave_layout.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "interfaces/storage/data_stream.h"
#include "interfaces/storage/resource.h"

enum {
	CD_CHANNELS = 2,
	CD_SAMPLE_BITS = 16,
	CD_FRAME_BYTES = CD_CHANNELS * CD_SAMPLE_BITS / 8,
	CD_SAMPLE_RATE = 44100,
	AUDIO_READ_BYTES = 65536
};

static void
wave_header(unsigned char bytes[MH_WAVE_PCM_HEADER_BYTES], unsigned long length)
{
	unsigned char *format;

	memset(bytes, 0, MH_WAVE_PCM_HEADER_BYTES);
	memcpy(bytes, "RIFF", MH_WAVE_FOURCC_BYTES);
	mh_write_u32le(bytes + MH_WAVE_CHUNK_SIZE,
		length + MH_WAVE_PCM_HEADER_BYTES - MH_WAVE_CHUNK_HEADER_BYTES);
	memcpy(bytes + MH_WAVE_CHUNK_HEADER_BYTES, "WAVE",
		MH_WAVE_FOURCC_BYTES);
	memcpy(bytes + MH_WAVE_HEADER_BYTES, "fmt ", MH_WAVE_FOURCC_BYTES);
	mh_write_u32le(bytes + MH_WAVE_HEADER_BYTES + MH_WAVE_CHUNK_SIZE,
		MH_WAVE_FORMAT_BYTES);
	format = bytes + MH_WAVE_PCM_FORMAT_OFFSET;
	mh_write_u16le(format, MH_WAVE_PCM_FORMAT);
	mh_write_u16le(format + MH_WAVE_FORMAT_CHANNELS, CD_CHANNELS);
	mh_write_u32le(format + MH_WAVE_FORMAT_RATE, CD_SAMPLE_RATE);
	mh_write_u32le(format + MH_WAVE_FORMAT_BYTE_RATE,
		(unsigned long)CD_SAMPLE_RATE * CD_FRAME_BYTES);
	mh_write_u16le(format + MH_WAVE_FORMAT_ALIGN, CD_FRAME_BYTES);
	mh_write_u16le(format + MH_WAVE_FORMAT_BITS, CD_SAMPLE_BITS);
	memcpy(bytes + MH_WAVE_PCM_DATA_CHUNK, "data", MH_WAVE_FOURCC_BYTES);
	mh_write_u32le(
		bytes + MH_WAVE_PCM_DATA_CHUNK + MH_WAVE_CHUNK_SIZE, length);
}

static enum mh_error_code
copy_audio(const struct mh_resource_source *source, const char *path,
	unsigned long start, unsigned long length, int big_endian,
	struct mh_data_stream *stream, struct mh_error *error_out)
{
	unsigned char bytes[AUDIO_READ_BYTES];
	unsigned long offset;
	size_t count, i;
	unsigned char saved;
	enum mh_error_code status;

	offset = 0;

	while (offset < length) {
		count = sizeof(bytes);

		if (length - offset < count)
			count = (size_t)(length - offset);

		status = mh_resource_source_read_at(
			source, path, start + offset, bytes, count, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (big_endian) {
			for (i = 0; i < count; i += 2) {
				saved = bytes[i];
				bytes[i] = bytes[i + 1];
				bytes[i + 1] = saved;
			}
		}

		status = mh_data_stream_append(stream, bytes, count, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		offset += count;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_cd_audio_extract_track(const struct mh_resource_source *source,
	const char *path, unsigned long start, unsigned long length,
	int big_endian, struct mh_data_directory *directory, unsigned int track,
	struct mh_error *error_out)
{
	unsigned char header[MH_WAVE_PCM_HEADER_BYTES];
	char name[32];
	unsigned long size;
	size_t payload_size;
	struct mh_data_stream *stream;
	struct mh_data_file *file;
	enum mh_error_code status;

	if (directory == NULL || track < 2 || track > 99)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid CD audio track");

	status = mh_resource_source_size(source, path, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (start > size || length > size - start)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"CD audio track exceeds image");

	if (length == 0)
		length = size - start;

	payload_size = (size_t)length;

	if (length == 0 || length % MH_DISC_SECTOR_BYTES != 0 ||
		length > MH_U32_MAX -
				(MH_WAVE_PCM_HEADER_BYTES -
					MH_WAVE_CHUNK_HEADER_BYTES) ||
		length != payload_size ||
		payload_size > (size_t)-1 - MH_WAVE_PCM_HEADER_BYTES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"CD audio has incomplete sectors or exceeds WAV size limits");

	stream = NULL;
	file = NULL;
	wave_header(header, length);
	status = mh_data_stream_open(directory, &stream, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_stream_append(
			stream, header, sizeof(header), error_out);

	if (status == MH_ERROR_NONE)
		status = copy_audio(source, path, start, length, big_endian,
			stream, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_stream_seal(stream, error_out);

	sprintf(name, MH_ASSET_CD_TRACK_NAME_FORMAT, track);

	if (status == MH_ERROR_NONE)
		status = mh_data_stream_create(
			stream, name, NULL, NULL, &file, error_out);

	mh_data_file_close(file);
	mh_data_stream_close(stream);

	return status;
}

enum mh_error_code
mh_cd_audio_extract(const struct mh_resource_source *source, const char *path,
	struct mh_data_directory *directory, unsigned int track,
	struct mh_error *error_out)
{
	return mh_cd_audio_extract_track(
		source, path, 0, 0, 0, directory, track, error_out);
}
