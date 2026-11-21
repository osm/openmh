#include <stdio.h>

#include "engine/audio/pcm.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "audio PCM:%d: %s\n", __LINE__, #x);   \
			return 1;                                              \
		}                                                              \
	} while (0)

/* No encoded container or decoder library is needed to consume PCM. */
int
main(void)
{
	static const unsigned char stereo[] = {
		0, 128, 255, 127, 255, 255, 0, 0};

	static const unsigned char mono[] = {0, 1, 128, 255};
	struct mh_audio_pcm pcm;

	pcm.pcm = stereo;
	pcm.bytes = sizeof(stereo);
	pcm.frames = 2;
	pcm.rate = 44100;
	pcm.channels = 2;
	pcm.bits = 16;
	CHECK(mh_audio_pcm_sample(&pcm, 0, 0) == -1);
	CHECK(mh_audio_pcm_sample(&pcm, 0, 1) == 32767.0 / 32768);
	CHECK(mh_audio_pcm_sample(&pcm, 1, 0) == -1.0 / 32768);
	CHECK(mh_audio_pcm_sample(&pcm, 1, 1) == 0);
	CHECK(mh_audio_pcm_sample(&pcm, 2, 0) == 0);
	CHECK(mh_audio_pcm_sample(&pcm, 0, 2) == 0);

	pcm.pcm = mono;
	pcm.bytes = sizeof(mono);
	pcm.frames = 4;
	pcm.channels = 1;
	pcm.bits = 8;
	CHECK(mh_audio_pcm_sample(&pcm, 0, 0) == -1);
	CHECK(mh_audio_pcm_sample(&pcm, 1, 0) == -127.0 / 128);
	CHECK(mh_audio_pcm_sample(&pcm, 2, 0) == 0);
	CHECK(mh_audio_pcm_sample(&pcm, 3, 0) == 127.0 / 128);
	CHECK(mh_audio_pcm_sample(&pcm, 0, 1) == 0);

	return 0;
}
