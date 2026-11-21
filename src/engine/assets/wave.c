#include <string.h>

#include "engine/assets/wave.h"
#include "engine/assets/wave_layout.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"

#define MIN_SAMPLE_RATE 4000UL
#define MAX_SAMPLE_RATE 192000UL
#define MAX_PCM_FRAMES 32000000UL

enum mh_error_code
mh_audio_wave_decode(const unsigned char *data, size_t size,
	struct mh_audio_pcm *output, struct mh_error *error_out)
{
	struct mh_audio_pcm wave;
	const unsigned char *payload;
	size_t offset, riff_end, payload_size;
	unsigned long chunk_size, format_tag, block_align, byte_rate;
	int have_format, have_data;

	if (data == NULL || output == NULL || size < MH_WAVE_HEADER_BYTES ||
		memcmp(data, "RIFF", MH_WAVE_FOURCC_BYTES) != 0 ||
		memcmp(data + MH_WAVE_CHUNK_HEADER_BYTES, "WAVE",
			MH_WAVE_FOURCC_BYTES) != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "expected RIFF/WAVE");

	chunk_size = mh_read_u32le(data + MH_WAVE_CHUNK_SIZE);

	if (chunk_size < MH_WAVE_FOURCC_BYTES ||
		chunk_size > size - MH_WAVE_CHUNK_HEADER_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "truncated RIFF/WAVE");

	riff_end = (size_t)chunk_size + MH_WAVE_CHUNK_HEADER_BYTES;
	offset = MH_WAVE_HEADER_BYTES;
	have_format = have_data = 0;
	format_tag = block_align = byte_rate = 0;
	memset(&wave, 0, sizeof(wave));

	while (offset < riff_end) {
		if (riff_end - offset < MH_WAVE_CHUNK_HEADER_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated WAVE chunk header");

		chunk_size = mh_read_u32le(data + offset + MH_WAVE_CHUNK_SIZE);

		if (chunk_size > riff_end - offset - MH_WAVE_CHUNK_HEADER_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated WAVE chunk");

		payload_size = (size_t)chunk_size;
		payload = data + offset + MH_WAVE_CHUNK_HEADER_BYTES;

		if (memcmp(data + offset, "fmt ", MH_WAVE_FOURCC_BYTES) == 0) {
			if (have_format || payload_size < MH_WAVE_FORMAT_BYTES)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid WAVE format chunk");

			format_tag = mh_read_u16le(payload);
			wave.channels = (unsigned int)mh_read_u16le(
				payload + MH_WAVE_FORMAT_CHANNELS);
			wave.rate =
				mh_read_u32le(payload + MH_WAVE_FORMAT_RATE);
			byte_rate = mh_read_u32le(
				payload + MH_WAVE_FORMAT_BYTE_RATE);
			block_align =
				mh_read_u16le(payload + MH_WAVE_FORMAT_ALIGN);
			wave.bits = (unsigned int)mh_read_u16le(
				payload + MH_WAVE_FORMAT_BITS);
			have_format = 1;
		} else if (memcmp(data + offset, "data",
				MH_WAVE_FOURCC_BYTES) == 0) {
			if (have_data || payload_size == 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid WAVE data chunk");

			wave.pcm = payload;
			wave.bytes = payload_size;
			have_data = 1;
		}

		offset += MH_WAVE_CHUNK_HEADER_BYTES + payload_size;

		if (payload_size % 2 != 0) {
			if (offset == riff_end)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"missing WAVE chunk padding");

			offset++;
		}
	}

	if (!have_format || !have_data)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WAVE format or data");

	if (format_tag != MH_WAVE_PCM_FORMAT ||
		(wave.channels != 1 && wave.channels != 2) ||
		(wave.bits != 8 && wave.bits != 16))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported WAVE format");

	if (wave.rate < MIN_SAMPLE_RATE || wave.rate > MAX_SAMPLE_RATE ||
		block_align != wave.channels * (wave.bits / 8) ||
		byte_rate != wave.rate * block_align ||
		wave.bytes % block_align != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid WAVE PCM layout");

	wave.frames = wave.bytes / (size_t)block_align;

	if (wave.frames > MAX_PCM_FRAMES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"WAVE exceeds native frame limit");

	*output = wave;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
