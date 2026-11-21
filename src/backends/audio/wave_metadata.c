#include <string.h>

#include "backends/audio/wave_metadata.h"
#include "engine/assets/wave_layout.h"
#include "engine/core/binary.h"
#include "interfaces/storage/resource.h"

#define WAVE_FILE_BYTES_MAX (256UL * 1024UL * 1024UL)
#define WAVE_CHUNKS_MAX 65536UL
#define WAVE_FRAMES_MAX 32000000UL
#define WAVE_RATE_MIN 4000UL
#define WAVE_RATE_MAX 192000UL

static enum mh_error_code
wave_format_read(const struct mh_resource_source *resources, const char *path,
	unsigned long offset, struct mh_audio_decode_info *info,
	struct mh_error *error_out)
{
	unsigned char bytes[MH_WAVE_FORMAT_BYTES];
	unsigned long alignment, byte_rate;
	enum mh_error_code status;

	status = mh_resource_source_read_at(
		resources, path, offset, bytes, sizeof(bytes), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	info->channels = mh_read_u16le(bytes + MH_WAVE_FORMAT_CHANNELS);
	info->rate = mh_read_u32le(bytes + MH_WAVE_FORMAT_RATE);
	byte_rate = mh_read_u32le(bytes + MH_WAVE_FORMAT_BYTE_RATE);
	alignment = mh_read_u16le(bytes + MH_WAVE_FORMAT_ALIGN);
	info->bits = mh_read_u16le(bytes + MH_WAVE_FORMAT_BITS);

	if (mh_read_u16le(bytes) != MH_WAVE_PCM_FORMAT ||
		(info->channels != 1 && info->channels != 2) ||
		(info->bits != 8 && info->bits != 16))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported WAVE format");

	if (info->rate < WAVE_RATE_MIN || info->rate > WAVE_RATE_MAX ||
		alignment != info->channels * (info->bits / 8) ||
		byte_rate != info->rate * alignment)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid WAVE PCM layout");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_audio_wave_inspect(void *context, const struct mh_resource_source *resources,
	const char *path, struct mh_audio_decode_info *output,
	struct mh_error *error_out)
{
	struct mh_audio_decode_info info;
	unsigned char header[MH_WAVE_HEADER_BYTES];
	unsigned long file_size, end, offset, bytes, chunks, data_bytes;
	size_t frame_bytes;
	int have_format, have_data;
	enum mh_error_code status;

	(void)context;
	status =
		mh_resource_source_size(resources, path, &file_size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (file_size < MH_WAVE_HEADER_BYTES || file_size > WAVE_FILE_BYTES_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or oversized WAVE file");

	status = mh_resource_source_read_at(
		resources, path, 0, header, MH_WAVE_HEADER_BYTES, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (memcmp(header, "RIFF", MH_WAVE_FOURCC_BYTES) != 0 ||
		memcmp(header + MH_WAVE_CHUNK_HEADER_BYTES, "WAVE",
			MH_WAVE_FOURCC_BYTES) != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "expected RIFF/WAVE");

	end = mh_read_u32le(header + MH_WAVE_CHUNK_SIZE);

	if (end < MH_WAVE_FOURCC_BYTES ||
		end > file_size - MH_WAVE_CHUNK_HEADER_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "truncated RIFF/WAVE");

	end += MH_WAVE_CHUNK_HEADER_BYTES;
	offset = MH_WAVE_HEADER_BYTES;
	chunks = data_bytes = 0;
	have_format = have_data = 0;
	memset(&info, 0, sizeof(info));

	while (offset < end) {
		chunks++;

		if (chunks > WAVE_CHUNKS_MAX)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many WAVE chunks");

		if (end - offset < MH_WAVE_CHUNK_HEADER_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated WAVE chunk header");

		status = mh_resource_source_read_at(resources, path, offset,
			header, MH_WAVE_CHUNK_HEADER_BYTES, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		bytes = mh_read_u32le(header + MH_WAVE_CHUNK_SIZE);

		if (bytes > end - offset - MH_WAVE_CHUNK_HEADER_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated WAVE chunk");

		if (memcmp(header, "fmt ", MH_WAVE_FOURCC_BYTES) == 0) {
			if (have_format || bytes < MH_WAVE_FORMAT_BYTES)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid WAVE format chunk");

			status = wave_format_read(resources, path,
				offset + MH_WAVE_CHUNK_HEADER_BYTES, &info,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;

			have_format = 1;
		} else if (memcmp(header, "data", MH_WAVE_FOURCC_BYTES) == 0) {
			if (have_data || bytes == 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid WAVE data chunk");

			have_data = 1;
			data_bytes = bytes;
		}

		offset += MH_WAVE_CHUNK_HEADER_BYTES + bytes;

		if (bytes % 2 != 0) {
			if (offset == end)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"missing WAVE chunk padding");

			offset++;
		}
	}

	if (!have_format || !have_data)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WAVE format or data");

	frame_bytes = info.channels * (info.bits / 8);

	if (data_bytes % frame_bytes != 0 ||
		data_bytes / frame_bytes > WAVE_FRAMES_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid WAVE data size");

	info.frames = (size_t)(data_bytes / frame_bytes);
	*output = info;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
