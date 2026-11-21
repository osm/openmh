#include <stdio.h>
#include <stdlib.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/wave.h"
#include "engine/audio/pcm.h"
#include "engine/core/error.h"

enum {
	PCM_BYTES = 2,
	BYTE_BITS = 8,
	BYTE_MASK = 255
};

#define PCM_SCALE 32768
#define PCM_MODULUS 65536L
#define ADLER_MODULUS 65521

/* Adler-32 of interleaved signed-16 little-endian decoded samples. */
static unsigned long
pcm_checksum(const struct mh_audio_pcm *wave)
{
	unsigned long a, b, word, octet;
	size_t i;
	unsigned int channel, byte;
	long value;

	a = 1;
	b = 0;

	for (i = 0; i < wave->frames; i++)
		for (channel = 0; channel < wave->channels; channel++) {
			value = (long)(mh_audio_pcm_sample(wave, i, channel) *
				PCM_SCALE);
			word = value < 0 ? (unsigned long)(PCM_MODULUS + value)
					: (unsigned long)value;

			for (byte = 0; byte < PCM_BYTES; byte++) {
				octet = (word >> (BYTE_BITS * byte)) &
					BYTE_MASK;
				a = (a + octet) % ADLER_MODULUS;
				b = (b + a) % ADLER_MODULUS;
			}
		}

	return (b << 16) | a;
}

int
main(int argc, char *argv[])
{
	struct mh_file file;
	struct mh_audio_pcm wave;
	struct mh_error error;
	enum mh_error_code status;

	if (argc != 2) {
		fprintf(stderr, "Usage: mh-wave-info FILE.wav\n");
		return 1;
	}

	status = mh_file_read(argv[1], &file, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	status = mh_audio_wave_decode(file.data, file.size, &wave, &error);

	if (status != MH_ERROR_NONE) {
		free(file.data);
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	printf("%lu %u %u %lu %08lx\n", wave.rate, wave.channels, wave.bits,
		(unsigned long)wave.frames, pcm_checksum(&wave));
	free(file.data);

	return ferror(stdout) ? 1 : 0;
}
