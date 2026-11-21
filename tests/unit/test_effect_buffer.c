#include <stdio.h>
#include <string.h>

#include "engine/audio/effect_buffer.h"
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

/* Callback fields captured by the mock audio driver. */
enum driver_trace {
	TRACE_STATUS = 0,
	TRACE_POSITION,
	TRACE_LOCK,
	TRACE_RESTORE,
	TRACE_UNLOCK,
	TRACE_LOCK_OFFSET,
	TRACE_LOCK_BYTES,
	TRACE_FIELDS
};

struct fixture {
	unsigned long		row[16], calls[TRACE_FIELDS];
	unsigned int		malformed;
	struct mh_effect_buffer	*buffer;
	unsigned char		output[4096];
};

static unsigned long
status(void *context, unsigned long *value)
{
	struct fixture *fixture;

	fixture = (struct fixture *)context;
	fixture->calls[TRACE_STATUS]++;
	*value = fixture->row[9];

	return 1; /* Deliberately ignored by the original poll. */
}

static unsigned long
position(void *context, unsigned long *play, unsigned long *write)
{
	struct fixture *fixture;

	fixture = (struct fixture *)context;
	fixture->calls[TRACE_POSITION]++;

	if (fixture->row[10])
		return 1;

	*play = fixture->row[2];
	*write = fixture->row[3];

	return 0;
}

static unsigned long
restore(void *context)
{
	struct fixture *fixture;

	fixture = (struct fixture *)context;
	fixture->calls[TRACE_RESTORE]++;

	return 1; /* Restore failure must not suppress the retry. */
}

static unsigned long
lock(void *context, unsigned long offset, unsigned long bytes,
	struct mh_effect_buffer_region regions[2])
{
	struct fixture *fixture;
	size_t first;

	fixture = (struct fixture *)context;
	fixture->calls[TRACE_LOCK]++;
	CHECK(bytes == 4096);

	if (fixture->calls[TRACE_LOCK] > 1)
		CHECK(offset == fixture->calls[TRACE_LOCK_OFFSET] &&
			bytes == fixture->calls[TRACE_LOCK_BYTES]);

	fixture->calls[TRACE_LOCK_OFFSET] = offset;
	fixture->calls[TRACE_LOCK_BYTES] = bytes;

	if (fixture->row[11] == 2)
		return 1;

	if (fixture->row[11] != 0) {
		if (fixture->calls[TRACE_LOCK] == 1)
			return MH_EFFECT_BUFFER_ERROR_LOST;

		if (fixture->row[11] == 3)
			return 1;
	}

	first = (size_t)(fixture->row[8] - offset % fixture->row[8]);

	if (first > 4096)
		first = 4096;

	regions[0].data = fixture->output;
	regions[0].bytes = first;
	regions[1].data = first == 4096 ? NULL : fixture->output + first;
	regions[1].bytes = 4096 - first;

	switch (fixture->malformed) {
	case 1:
		regions[0].data = NULL;
		break;

	case 2:
		regions[0].bytes = 0;
		regions[1].bytes = 4096;
		break;

	case 3:
		regions[0].bytes = 4097;
		break;

	case 4:
		regions[0].bytes = 4095;
		regions[1].bytes = 1;
		break;

	case 5:
		regions[1].bytes--;
		break;

	case 6:
		regions[1].bytes = 0;
		regions[0].bytes = 4096;
		break;

	case 7:
		regions[1].data = NULL;
		break;

	default:
		break;
	}

	return 0;
}

static unsigned long
unlock(void *context, const struct mh_effect_buffer_region regions[2])
{
	struct fixture *fixture;

	fixture = (struct fixture *)context;
	fixture->calls[TRACE_UNLOCK]++;

	if (!fixture->malformed) {
		CHECK(fixture->buffer->first_start ==
			fixture->calls[TRACE_LOCK_OFFSET]);
		CHECK(fixture->buffer->first_end ==
			((fixture->calls[TRACE_LOCK_OFFSET] +
				regions[0].bytes) &
				MH_U32_MAX));
		CHECK(fixture->buffer->second_start == 0 &&
			fixture->buffer->second_end == regions[1].bytes);
	}

	return fixture->row[12];
}

static const struct mh_effect_buffer_driver driver = {
	status, position, restore, lock, unlock};
static unsigned char source[64000];

static void
setup_fixture(struct fixture *fixture, struct mh_effect_buffer *buffer,
	struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS],
	struct mh_audio_pcm *wave)
{
	memset(fixture->calls, 0, sizeof(fixture->calls));

	fixture->malformed = 0;
	fixture->buffer = buffer;
	memset(fixture->output, 0xa5, sizeof(fixture->output));
	buffer->first_start = fixture->row[4];
	buffer->first_end = fixture->row[5];
	buffer->second_start = fixture->row[6];
	buffer->second_end = fixture->row[7];
	buffer->play = 1111;
	buffer->write = 2222;
	memset(wave, 0, sizeof(*wave));
	wave->pcm = source;
	wave->bytes = sizeof(source);
	wave->frames = (size_t)fixture->row[13];
	wave->rate = 22050;
	wave->channels = 1;
	wave->bits = 16;
	memset(channels, 0, sizeof(*channels) * MH_SOUND_CHANNELS);
	channels[0].wave = wave;
	channels[0].cursor = (size_t)fixture->row[14];
	channels[0].loop_end = wave->frames;
	channels[0].whole_step = 1;
	channels[0].flags = (unsigned int)fixture->row[15];
	channels[0].left = channels[0].right = .125f;
}

static void
cases(void)
{
	struct fixture fixture;
	struct mh_effect_buffer buffer, saved;
	struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS],
		prior[MH_SOUND_CHANNELS];
	struct mh_audio_pcm wave;
	unsigned char output[4096];
	unsigned int i;
	int filled;

	memset(&fixture, 0, sizeof(fixture));
	fixture.row[1] = 1;
	fixture.row[2] = 400;
	fixture.row[3] = 6144;
	fixture.row[5] = 4096;
	fixture.row[8] = 8192;
	fixture.row[13] = 1500;
	fixture.row[14] = 600;
	fixture.row[15] = 1;
	setup_fixture(&fixture, &buffer, channels, &wave);
	CHECK(mh_effect_buffer_fill(&buffer, channels, &driver, &fixture, 6144,
		&filled, NULL) == MH_ERROR_NONE &&
		filled);
	CHECK(buffer.first_start == 6144 && buffer.first_end == 8192 &&
		buffer.second_end == 2048);

	/*
	 * Completion in region two preserves 1112, its own entry cursor. A
	 * single combined 1024-frame call would incorrectly retain 600.
	 */
	CHECK(channels[0].flags == 0 && channels[0].cursor == 1112);

	for (i = 0; i < 1024; i++)
		CHECK(mh_read_u32le(fixture.output + i * 4) ==
			(i < 900 ? 0x08000800UL : 0));

	memset(fixture.calls, 0, sizeof(fixture.calls));
	fixture.row[2] = 100;
	CHECK(mh_effect_buffer_poll(&buffer, channels, &driver, &fixture,
		NULL) == MH_ERROR_NONE);
	CHECK(fixture.calls[TRACE_LOCK] == 1 &&
		fixture.calls[TRACE_LOCK_OFFSET] == 2048 &&
		buffer.first_end == 6144 && buffer.second_end == 0);

	/* The requested offset, not its modulo ring size, is published. */
	memset(fixture.calls, 0, sizeof(fixture.calls));
	CHECK(mh_effect_buffer_fill(&buffer, channels, &driver, &fixture, 8192,
		&filled, NULL) == MH_ERROR_NONE &&
		filled);
	CHECK(buffer.first_start == 8192 && buffer.first_end == 12288);
	fixture.row[2] = 0;
	memset(fixture.calls, 0, sizeof(fixture.calls));
	CHECK(mh_effect_buffer_poll(&buffer, channels, &driver, &fixture,
		NULL) == MH_ERROR_NONE &&
		fixture.calls[TRACE_LOCK] == 0);

	fixture.row[12] = 1;
	setup_fixture(&fixture, &buffer, channels, &wave);
	CHECK(mh_effect_buffer_fill(&buffer, channels, &driver, &fixture, 6144,
		&filled, NULL) == MH_ERROR_NONE &&
		!filled);
	CHECK(channels[0].cursor == 1112 && buffer.second_end == 2048 &&
		fixture.calls[TRACE_UNLOCK] == 1);

	for (i = 1; i <= 7; i++) {
		setup_fixture(&fixture, &buffer, channels, &wave);
		fixture.malformed = i;
		saved = buffer;
		memcpy(prior, channels, sizeof(prior));
		memcpy(output, fixture.output, sizeof(output));
		CHECK(mh_effect_buffer_fill(&buffer, channels, &driver,
			&fixture, 6144, &filled, NULL) != MH_ERROR_NONE &&
			!filled);
		CHECK(memcmp(&buffer, &saved, sizeof(buffer)) == 0 &&
			memcmp(prior, channels, sizeof(prior)) == 0);
		CHECK(memcmp(output, fixture.output, sizeof(output)) == 0 &&
			fixture.calls[TRACE_UNLOCK] == 1);
	}

	setup_fixture(&fixture, &buffer, channels, &wave);
	wave.bytes = 1;
	CHECK(mh_effect_buffer_fill(&buffer, channels, &driver, &fixture, 6144,
		&filled, NULL) != MH_ERROR_NONE &&
		fixture.calls[TRACE_LOCK] == 0);
	CHECK(mh_effect_buffer_poll(NULL, NULL, NULL, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effect_buffer_poll(NULL, channels, &driver, &fixture, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_effect_buffer_fill(&buffer, channels, NULL, &fixture, 0,
		&filled, NULL) != MH_ERROR_NONE);
}

static void
connected_lifecycle(void)
{
	struct fixture fixture;
	struct mh_effect_buffer buffer;
	struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS];
	struct mh_effects_mixer effects, saved;
	struct mh_sound_pool pool;
	struct mh_mixer bank, *mixers[1];
	struct mh_audio_pcm wave;
	float preview[2];
	int filled;

	memset(&fixture, 0, sizeof(fixture));
	fixture.row[1] = 1;
	fixture.row[8] = 8192;
	fixture.row[13] = 1500;
	setup_fixture(&fixture, &buffer, channels, &wave);
	mixers[0] = &bank;
	CHECK(mh_sound_pool_init(MH_MIXER_VOICES, &pool, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_init(48000, .125, &effects, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_init(48000, .125, &bank, NULL) == MH_ERROR_NONE &&
		mh_mixer_bind(&bank, &pool, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(&bank, 0, &wave, 1, 1, 1, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_buffer_fill(&effects, &pool, mixers, 1, &buffer,
		&driver, &fixture, 6144, &filled,
		NULL) == MH_ERROR_NONE &&
		filled);
	CHECK(effects.cursors[0] == 1024 && bank.voices[0].cursor == 1024 &&
		effects.pcm.voices[0].active);
	memset(fixture.calls, 0, sizeof(fixture.calls));
	fixture.row[2] = 100;
	CHECK(mh_effects_buffer_poll(&effects, &pool, mixers, 1, &buffer,
		&driver, &fixture, NULL) == MH_ERROR_NONE);
	CHECK(!effects.pcm.voices[0].active && bank.voices[0].active &&
		pool.records[0].active);
	CHECK(effects.cursors[0] == 1024 && bank.voices[0].cursor == 1024);
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(!bank.voices[0].active && !pool.records[0].active);
	CHECK(mh_mixer_play(&bank, 0, &wave, 1, 1, 1, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_render(&effects, &pool, mixers, 1, preview, 1, 0,
		NULL) == MH_ERROR_NONE &&
		effects.output_ready);
	saved = effects;
	memset(fixture.calls, 0, sizeof(fixture.calls));
	filled = 7;
	CHECK(mh_effects_buffer_fill(&effects, &pool, mixers, 1, &buffer,
		&driver, &fixture, 0, &filled, NULL) != MH_ERROR_NONE);
	CHECK(fixture.calls[TRACE_LOCK] == 0 && filled == 7 &&
		memcmp(&effects, &saved, sizeof(effects)) == 0);
}

int
main(void)
{
	size_t i;

	for (i = 0; i < sizeof(source); i += 2) {
		source[i] = 0;
		source[i + 1] = 64;
	}

	cases();
	connected_lifecycle();

	return failures != 0;
}
