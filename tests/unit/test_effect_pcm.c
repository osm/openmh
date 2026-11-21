#include <stdio.h>
#include <string.h>

#include "engine/audio/effect_pcm.h"
#include "engine/audio/effects.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "%d: %s\n", __LINE__, #x);             \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
setup_channels(struct mh_effect_pcm_channel *channels,
	struct mh_audio_pcm *wave, const unsigned char *pcm, size_t frames)
{
	memset(wave, 0, sizeof(*wave));

	wave->pcm = pcm;
	wave->frames = frames;
	wave->bytes = frames * 2;
	wave->bits = 16;
	wave->channels = 1;
	wave->rate = 22050;
	memset(channels, 0, sizeof(*channels) * MH_SOUND_CHANNELS);
	channels[0].wave = wave;
	channels[0].flags = 1;
	channels[0].whole_step = 1;
	channels[0].loop_end = frames;
	channels[0].left = channels[0].right = .125f;
}

static void
cases(void)
{
	struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS],
		saved[MH_SOUND_CHANNELS];
	struct mh_audio_pcm wave;
	unsigned char output[48], before[48];
	static const unsigned char pcm[] = {
		4, 0, 12, 0, 252, 255, 244, 255, 8, 0, 248, 255};

	static const unsigned long expected[] = {0, 2, 0, 65534, 1, MH_U16_MAX};
	unsigned int i;

	setup_channels(channels, &wave, pcm, 6);
	CHECK(mh_effect_pcm_render(channels, output, 6, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 6; i++) {
		CHECK(mh_read_u16le(output + i * 4) == expected[i]);
		CHECK(mh_read_u16le(output + i * 4 + 2) == expected[i]);
	}

	CHECK(channels[0].flags == 0 && channels[0].cursor == 0 &&
		channels[0].fraction == 0);
	setup_channels(channels, &wave, pcm, 6);
	channels[0].fraction = MH_U32_SIGN_BIT;
	CHECK(mh_effect_pcm_render(channels, output, 2, NULL) == MH_ERROR_NONE);
	CHECK(channels[0].cursor == 2 &&
		channels[0].fraction == MH_U32_SIGN_BIT);
	CHECK(mh_effect_pcm_render(channels, output, 8, NULL) == MH_ERROR_NONE);
	CHECK(channels[0].flags == 0 && channels[0].cursor == 2 &&
		channels[0].fraction == MH_U32_SIGN_BIT);

	for (i = 4 * 4; i < 8 * 4; i++)
		CHECK(output[i] == 0);

	/*
	 * Opposite-sign left samples cancel, but their packed unsigned sum
	 * carries one unit into the otherwise-silent right channel.
	 */
	setup_channels(channels, &wave, pcm, 6);
	channels[0].cursor = 4;
	channels[0].right = 0;
	channels[1] = channels[0];
	channels[1].cursor = 5;
	CHECK(mh_effect_pcm_render(channels, output, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_read_u32le(output) == 0x00010000UL);
	CHECK(channels[0].cursor == 5 && channels[1].cursor == 5 &&
		channels[1].flags == 0);

	/* Fractional carry and one subtraction at the exclusive loop end. */
	setup_channels(channels, &wave, pcm, 6);
	channels[0].flags = 7;
	channels[0].cursor = 3;
	channels[0].loop_start = 1;
	channels[0].loop_end = 4;
	channels[0].fraction = channels[0].fraction_step = MH_U32_SIGN_BIT;
	CHECK(mh_effect_pcm_render(channels, output, 1, NULL) == MH_ERROR_NONE);
	CHECK(channels[0].cursor == 2 && channels[0].fraction == 0 &&
		channels[0].flags == 7);

	/* Validate every active channel before producing any samples. */
	channels[7] = channels[0];
	channels[7].cursor = wave.frames;
	memcpy(saved, channels, sizeof(saved));
	memset(output, 0x5a, sizeof(output));
	memcpy(before, output, sizeof(before));
	CHECK(mh_effect_pcm_render(channels, output, 4, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved, channels, sizeof(saved)) == 0 &&
		memcmp(before, output, sizeof(output)) == 0);
	channels[7].cursor = 0;
	wave.bytes -= 1;
	CHECK(mh_effect_pcm_render(channels, output, 4, NULL) != MH_ERROR_NONE);
	wave.bytes += 1;
	channels[7].left = .126f;
	CHECK(mh_effect_pcm_render(channels, output, 4, NULL) != MH_ERROR_NONE);
	channels[7].left = .125f;
	channels[7].whole_step = 3;
	CHECK(mh_effect_pcm_render(channels, output, 4, NULL) != MH_ERROR_NONE);
	CHECK(mh_effect_pcm_render(NULL, output, 1, NULL) != MH_ERROR_NONE &&
		mh_effect_pcm_render(channels, NULL, 1, NULL) != MH_ERROR_NONE);
	CHECK(mh_effect_pcm_render(channels, output, 8193, NULL) !=
		MH_ERROR_NONE);
	memset(channels, 0, sizeof(channels));
	CHECK(mh_effect_pcm_render(channels, output, 0, NULL) ==
			MH_ERROR_NONE &&
		memcmp(before, output, sizeof(output)) == 0);
	CHECK(mh_effect_pcm_render(channels, output, 12, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < sizeof(output); i++)
		CHECK(output[i] == 0);

	setup_channels(channels, &wave, pcm, 1);
	channels[0].flags = 3;
	CHECK(mh_effect_pcm_render(channels, output, 12, NULL) ==
			MH_ERROR_NONE &&
		channels[0].cursor == 0);
}

/* Fixture schema remains independent of the native channel structure. */

int
main(void)
{

	cases();

	return failures != 0;
}
