#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/audio/effect_output.h"
#include "engine/audio/effects.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Ring output line %d: %s\n", __LINE__, \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static unsigned char ring[MH_EFFECT_OUTPUT_BYTES],
	saved_ring[MH_EFFECT_OUTPUT_BYTES];
static float whole[16384], pieces[16384];

static long
ramp(unsigned long frame, unsigned int channel)
{
	return (long)(((frame % 4096) * 37 + channel * 173) % 65536) - 32768L;
}

static void
pattern(void)
{
	unsigned long frame, value;
	unsigned int channel;

	for (frame = 0; frame < 4096; frame++) {
		for (channel = 0; channel < 2; channel++) {
			value = (unsigned long)ramp(frame, channel) &
				(unsigned long)MH_U16_MAX;
			ring[frame * 4 + channel * 2] = (unsigned char)value;
			ring[frame * 4 + channel * 2 + 1] =
				(unsigned char)(value >> 8);
		}
	}
}

static int
conversion(void)
{
	static const unsigned long rates[] = {
		4000, 8000, 11025, 22050, 44100, 48000, 192000};

	struct mh_effect_output output, chunked, before;
	unsigned long numerator, source_frame, fraction;
	size_t i, rendered_frames, count;
	unsigned int rate, channel;
	double first, next, expected;

	for (rate = 0; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
		CHECK(mh_effect_output_init(ring, sizeof(ring),
			MH_EFFECT_OUTPUT_LEAD, rates[rate], &output,
			NULL) == MH_ERROR_NONE);
		pattern();
		chunked = output;

		/*
		 * Start near the end with a fractional phase to cross the ring
		 * seam even at high output rates. Reference time is absolute,
		 * independent of the converter's incremental cursor/phase
		 * recurrence.
		 */
		output.play_offset = chunked.play_offset = 16376;
		output.phase = chunked.phase = rates[rate] / 3;
		CHECK(mh_effect_output_render(&output, .125, whole, 8192, 0,
			NULL) == MH_ERROR_NONE);
		rendered_frames = 0;

		while (rendered_frames < 8192) {
			count = 1 + (rendered_frames * 17) % 701;

			if (count > 8192 - rendered_frames)
				count = 8192 - rendered_frames;

			CHECK(mh_effect_output_render(&chunked, .125,
				pieces + rendered_frames * 2, count, 0,
				NULL) == MH_ERROR_NONE);
			rendered_frames += count;
		}

		CHECK(memcmp(whole, pieces, sizeof(whole)) == 0);
		CHECK(output.play_offset == chunked.play_offset &&
			output.phase == chunked.phase &&
			output.consumed_frames == chunked.consumed_frames);

		for (i = 0; i < 8192; i++) {
			numerator = (unsigned long)i * 22050 + rates[rate] / 3;
			source_frame = 4094 + numerator / rates[rate];
			fraction = numerator % rates[rate];

			for (channel = 0; channel < 2; channel++) {
				first = (double)ramp(source_frame, channel);
				next = (double)ramp(source_frame + 1, channel);
				expected = (first * (rates[rate] - fraction) +
						next * fraction) /
					rates[rate] / 32768;
				CHECK(fabs(whole[i * 2 + channel] - expected) <
					0.00000007);
			}
		}

		numerator = 8192UL * 22050 + rates[rate] / 3;
		CHECK(output.play_offset ==
			((4094 + numerator / rates[rate]) % 4096) * 4);
		CHECK(output.consumed_frames == numerator / rates[rate] &&
			output.phase == numerator % rates[rate]);
		before = output;
		CHECK(mh_effect_output_render(&output, .125, whole, 37, 1,
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&before, &output, sizeof(output)) == 0);

		for (i = 0; i < 74; i++)
			CHECK(whole[i] == 0);

		CHECK(mh_effect_output_render(&output, 0, whole, 37, 0, NULL) ==
			MH_ERROR_NONE);

		for (i = 0; i < 74; i++)
			CHECK(whole[i] == 0);

		CHECK(output.consumed_frames > before.consumed_frames);
	}

	return 0;
}

static int
lifecycle(void)
{
	unsigned char source[8192];
	struct mh_audio_pcm wave;
	struct mh_effect_output output, before;
	struct mh_effects_mixer effects;
	struct mh_sound_pool pool;
	struct mh_mixer bank, *mixers[1];
	unsigned long filled_at, expected;
	size_t i, audible;
	int filled;

	memset(&wave, 0, sizeof(wave));

	for (i = 0; i < sizeof(source); i += 2) {
		source[i] = 0;
		source[i + 1] = 64;
	}

	wave.pcm = source;
	wave.bytes = sizeof(source);
	wave.frames = sizeof(source) / 2;
	wave.rate = 22050;
	wave.bits = 16;
	wave.channels = 1;
	mixers[0] = &bank;
	CHECK(mh_sound_pool_init(MH_MIXER_VOICES, &pool, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_init(22050, .125, &bank, NULL) == MH_ERROR_NONE &&
		mh_mixer_bind(&bank, &pool, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_init(22050, .125, &effects, NULL) == MH_ERROR_NONE);
	CHECK(mh_effect_output_init(ring, sizeof(ring), MH_EFFECT_OUTPUT_LEAD,
		22050, &output, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_buffer_fill(&effects, &pool, mixers, 1, &output.buffer,
		mh_effect_output_driver(), &output, output.buffer.write,
		&filled, NULL) == MH_ERROR_NONE &&
		filled);
	CHECK(output.fills == 1 && output.buffer.first_start == 2048 &&
		output.buffer.first_end == 6144);
	CHECK(mh_mixer_play(&bank, 0, &wave, 1, 1, 1, 0, wave.frames, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effect_output_render(&output, .125, whole, 882, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 1764; i++)
		CHECK(whole[i] == 0);

	CHECK(effects.cursors[0] == 0 && output.play_offset == 3528);
	CHECK(mh_effects_buffer_poll(&effects, &pool, mixers, 1, &output.buffer,
		mh_effect_output_driver(), &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.fills == 2 && effects.cursors[0] == 1024);
	CHECK(mh_effect_output_render(&output, .125, whole, 882, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 882; i++) {
		CHECK(whole[i * 2] == (i < 654 ? 0 : .0625f));
		CHECK(whole[i * 2 + 1] == whole[i * 2]);
	}

	CHECK(effects.cursors[0] == 1024); /* Playback never mixes channels. */
	mh_mixer_stop(&bank, 0);
	CHECK(mh_effects_flush_stops(&effects, &pool, NULL) == MH_ERROR_NONE &&
		!effects.pcm.voices[0].active);

	/* Stop retains 796 audible frames that were already mixed. */
	CHECK(mh_effect_output_render(&output, .0625, whole, 800, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 800; i++) {
		CHECK(whole[i * 2] == (i < 796 ? .03125f : 0));
		CHECK(whole[i * 2 + 1] == whole[i * 2]);
	}

	/*
	 * Miss the entire fill interval. No catch-up, physical advance or ring
	 * erase: the previously mixed block repeats on the next revolution.
	 */
	CHECK(output.play_offset == 10256);
	CHECK(mh_effects_buffer_poll(&effects, &pool, mixers, 1, &output.buffer,
		mh_effect_output_driver(), &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.fills == 2 && effects.cursors[0] == 1024);
	CHECK(mh_effect_output_render(&output, .125, whole, 4096, 0, NULL) ==
		MH_ERROR_NONE);
	audible = 0;

	for (i = 0; i < 4096; i++) {
		CHECK(whole[i * 2] == 0 || whole[i * 2] == .0625f);
		CHECK(whole[i * 2 + 1] == whole[i * 2]);
		audible += whole[i * 2] != 0;
	}

	CHECK(audible == 1024 && output.fills == 2 &&
		effects.cursors[0] == 1024);
	before = output;
	CHECK(mh_effect_output_clear(&output, NULL) == MH_ERROR_NONE &&
		memcmp(&before, &output, sizeof(output)) == 0);
	CHECK(mh_effect_output_render(&output, .125, whole, 4096, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 8192; i++)
		CHECK(whole[i] == 0);

	/*
	 * Supplied 40 ms schedule, not a wall-clock timing assertion.
	 * Half-block startup lead avoids the original unnormalized
	 * exact-capacity endpoint.
	 */
	CHECK(mh_effect_output_init(ring, sizeof(ring), MH_EFFECT_OUTPUT_LEAD,
		22050, &output, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_buffer_fill(&effects, &pool, mixers, 1, &output.buffer,
		mh_effect_output_driver(), &output, output.buffer.write,
		&filled, NULL) == MH_ERROR_NONE &&
		filled);
	CHECK(mh_mixer_play(&bank, 0, &wave, 1, 1, 1, 0, wave.frames, NULL) ==
		MH_ERROR_NONE);

	for (i = 1; i <= 500; i++) {
		filled_at = output.fills;
		CHECK(mh_effect_output_render(&output, .125, whole, 882, 0,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_effects_buffer_poll(&effects, &pool, mixers, 1,
			&output.buffer, mh_effect_output_driver(),
			&output, NULL) == MH_ERROR_NONE);
		expected = 2 + ((unsigned long)i * 882 - 513) / 1024;
		CHECK(output.fills == expected &&
			output.fills <= filled_at + 1);
		CHECK(effects.cursors[0] ==
			((output.fills - 1) * 1024) % wave.frames);
	}

	CHECK(output.wraps > 100);

	return 0;
}

static int
invalid(void)
{
	struct mh_effect_output output, before;
	struct mh_effect_buffer_region regions[2];
	const struct mh_effect_buffer_driver *driver;
	size_t i;

	CHECK(mh_effect_output_init(ring, sizeof(ring), MH_EFFECT_OUTPUT_LEAD,
		48000, &output, NULL) == MH_ERROR_NONE);
	pattern();
	memcpy(saved_ring, ring, sizeof(ring));
	before = output;
	CHECK(mh_effect_output_init(ring, sizeof(ring), sizeof(ring), 48000,
		&output, NULL) != MH_ERROR_NONE);
	CHECK(mh_effect_output_init(ring, sizeof(ring) - 1, 0, 48000, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_effect_output_init(ring, 4092, 0, 48000, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_effect_output_init(NULL, sizeof(ring), 0, 48000, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_effect_output_init(ring, sizeof(ring), 0, 3999, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_effect_output_init(ring, sizeof(ring), 0, 192001, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&output, &before, sizeof(output)) == 0 &&
		memcmp(saved_ring, ring, sizeof(ring)) == 0);

	for (i = 0; i < sizeof(whole) / sizeof(whole[0]); i++)
		whole[i] = .75f;

	CHECK(mh_effect_output_render(&output, -1, whole, 1, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_effect_output_render(&output, 2, whole, 1, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_effect_output_render(&output, .125, whole, 8193, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_effect_output_render(&output, .125, NULL, 1, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&output, &before, sizeof(output)) == 0 &&
		memcmp(saved_ring, ring, sizeof(ring)) == 0);

	for (i = 0; i < sizeof(whole) / sizeof(whole[0]); i++)
		CHECK(whole[i] == .75f);

	driver = mh_effect_output_driver();
	CHECK(driver->lock(&output, 14336, 4096, regions) == 0);
	CHECK(mh_effect_output_render(&output, .125, whole, 1, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_effect_output_clear(&output, NULL) != MH_ERROR_NONE &&
		memcmp(saved_ring, ring, sizeof(ring)) == 0);
	CHECK(driver->unlock(&output, regions) == 0 && output.wraps == 1);

	return 0;
}

int
main(void)
{
	return conversion() || lifecycle() || invalid();
}
