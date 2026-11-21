#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/wave_provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/audio/music.h"
#include "engine/audio/music_catalog.h"
#include "engine/audio/pcm.h"
#include "engine/audio/sample.h"
#include "interfaces/audio/decoder_backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "audio decoder:%d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	unsigned int	inspections, opens, closes;
	int		fail, bad_info;
};

struct sample_decoder {
	size_t	position;
};

static int
check_wave(void)
{
	static const unsigned char wave[] = {'R', 'I', 'F', 'F', 44, 0, 0, 0,
		'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
		0x44, 0xac, 0, 0, 0x10, 0xb1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't',
		'a', 8, 0, 0, 0, 0, 0x40, 0, 0xc0, 0xff, 0x7f, 0, 0x80};

	struct mh_resource_entry entry;
	const struct mh_resource_source *resources;
	const struct mh_audio_decoder_provider *provider;
	struct mh_audio_decode_info info, before;
	struct mh_audio_decoder *decoder, *other;
	unsigned char pcm[12];
	size_t count;

	entry.path = "sample.data";
	entry.data = wave;
	entry.size = sizeof(wave);
	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	provider = mh_audio_wave_decoder();
	CHECK(strcmp(mh_audio_decoder_extension(provider), "wav") == 0);
	CHECK(mh_audio_decoder_inspect(provider, resources, entry.path, &info,
		NULL) == MH_ERROR_NONE);
	CHECK(info.frames == 2 && info.rate == 44100 && info.channels == 2 &&
		info.bits == 16);
	before = info;
	CHECK(mh_audio_decoder_inspect(provider, resources, "missing", &info,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(info.frames == before.frames && info.rate == before.rate);
	CHECK(mh_audio_decoder_open(provider, resources, entry.path, &decoder,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_audio_decoder_open(provider, resources, entry.path, &other,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_audio_decoder_info(decoder)->frames == info.frames);
	count = 99;
	CHECK(mh_audio_decoder_read(decoder, pcm, 3, 1, &count, NULL) ==
		MH_ERROR_RANGE);
	CHECK(count == 99);
	CHECK(mh_audio_decoder_read(decoder, pcm, sizeof(pcm), 3, &count,
		NULL) == MH_ERROR_NONE);
	CHECK(count == 2 && memcmp(pcm, wave + 44, 8) == 0);
	CHECK(mh_audio_decoder_read(decoder, pcm, sizeof(pcm), 1, &count,
		NULL) == MH_ERROR_NONE);
	CHECK(count == 0);
	CHECK(mh_audio_decoder_seek(decoder, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_audio_decoder_seek(decoder, 3, NULL) == MH_ERROR_RANGE);
	CHECK(mh_audio_decoder_read(decoder, pcm, sizeof(pcm), 1, &count,
		NULL) == MH_ERROR_NONE);
	CHECK(count == 1 && memcmp(pcm, wave + 48, 4) == 0);
	CHECK(mh_audio_decoder_read(other, pcm, sizeof(pcm), 1, &count, NULL) ==
		MH_ERROR_NONE);
	CHECK(count == 1 && memcmp(pcm, wave + 44, 4) == 0);
	mh_audio_decoder_close(other);
	mh_audio_decoder_close(decoder);
	mh_memory_resources_close(resources);

	return 0;
}

static int
check_mono(void)
{
	static const unsigned char wave[] = {'R', 'I', 'F', 'F', 40, 0, 0, 0,
		'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
		0x40, 0x1f, 0, 0, 0x40, 0x1f, 0, 0, 1, 0, 8, 0, 'd', 'a', 't',
		'a', 4, 0, 0, 0, 0, 128, 255, 64};

	struct mh_resource_entry entry;
	const struct mh_resource_source *resources;
	struct mh_audio_decoder *decoder;
	struct mh_audio_decode_info info;
	unsigned char pcm[4];
	size_t count;

	entry.path = "effect.data";
	entry.data = wave;
	entry.size = sizeof(wave);
	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_audio_decoder_inspect(mh_audio_wave_decoder(), resources,
		entry.path, &info, NULL) == MH_ERROR_NONE);
	CHECK(info.frames == 4 && info.rate == 8000 && info.channels == 1 &&
		info.bits == 8);
	CHECK(mh_audio_decoder_open(mh_audio_wave_decoder(), resources,
		entry.path, &decoder, NULL) == MH_ERROR_NONE);
	CHECK(mh_audio_decoder_read(decoder, pcm, sizeof(pcm), 4, &count,
		NULL) == MH_ERROR_NONE);
	CHECK(count == 4 && memcmp(pcm, wave + 44, 4) == 0);
	mh_audio_decoder_close(decoder);
	mh_memory_resources_close(resources);

	return 0;
}

static enum mh_error_code
sample_inspect(void *context, const struct mh_resource_source *resources,
	const char *path, struct mh_audio_decode_info *info,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)resources;
	fixture = context;
	fixture->inspections++;

	if (strcmp(path, "music/track-02.sample") != 0)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "test audio path");

	info->rate = 44100;
	info->frames = 44101;
	info->bits = 16;
	info->channels = 2;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sample_open(void *context, const struct mh_resource_source *resources,
	const char *path, void **output, struct mh_audio_decode_info *info,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	struct sample_decoder *decoder;
	struct mh_audio_decode_info next;
	enum mh_error_code status;

	fixture = context;
	status = sample_inspect(context, resources, path, &next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	decoder = calloc(1, sizeof(*decoder));

	if (decoder == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test allocation");

	fixture->opens++;

	if (fixture->bad_info)
		next.channels = 3;

	*info = next;
	*output = decoder;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sample_read(void *context, void *token, void *output, size_t requested,
	size_t *count, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct sample_decoder *decoder;

	(void)requested;
	fixture = context;
	decoder = token;

	if (fixture->fail)
		return mh_error_set(
			error_out, MH_ERROR_IO, "test decoding failure");

	/* Deliberately short reads; no container parser participates. */
	memset(output, (int)(decoder->position & 255), 4);
	decoder->position++;
	*count = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sample_seek(
	void *context, void *token, size_t frame, struct mh_error *error_out)
{
	struct sample_decoder *decoder;

	(void)context;
	decoder = token;
	decoder->position = frame;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
sample_close(void *context, void *token)
{
	struct fixture *fixture;

	fixture = context;
	fixture->closes++;
	free(token);
}

static int
check_selection(void)
{
	static const struct mh_audio_decoder_ops ops = {sample_inspect,
		sample_open, sample_read, sample_seek, sample_close};

	struct mh_audio_decoder_provider provider;
	struct mh_resource_entry entry;
	const struct mh_resource_source *resources;
	struct mh_audio_decoder *decoder;
	struct mh_music_catalog catalog;
	struct fixture fixture;
	unsigned char pcm[8];
	size_t count;

	memset(&fixture, 0, sizeof(fixture));
	memset(&catalog, 0, sizeof(catalog));
	provider.resolve = NULL;
	provider.extension = "sample";
	provider.ops = &ops;
	provider.context = &fixture;
	entry.path = "music/track-02.sample";
	entry.data = (const unsigned char *)"fixture";
	entry.size = 7;
	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	catalog.seconds[8] = 99;
	CHECK(mh_music_catalog_scan_from(&provider, resources, NULL, "music",
		&catalog, NULL) == MH_ERROR_NONE);
	CHECK(catalog.last_track == 2 && catalog.seconds[2] == 1 &&
		catalog.seconds[8] == 99);
	CHECK(fixture.inspections == 1 && fixture.opens == 0);
	CHECK(mh_audio_decoder_open(&provider, resources, entry.path, &decoder,
		NULL) == MH_ERROR_NONE);
	fixture.fail = 1;
	count = 99;
	CHECK(mh_audio_decoder_read(decoder, pcm, sizeof(pcm), 2, &count,
		NULL) == MH_ERROR_IO &&
		count == 99);
	fixture.fail = 0;
	CHECK(mh_audio_decoder_read(decoder, pcm, sizeof(pcm), 2, &count,
		NULL) == MH_ERROR_NONE);
	CHECK(count == 1 && pcm[0] == 0);
	CHECK(mh_audio_decoder_seek(decoder, 7, NULL) == MH_ERROR_NONE);
	CHECK(mh_audio_decoder_read(decoder, pcm, sizeof(pcm), 1, &count,
		NULL) == MH_ERROR_NONE &&
		pcm[0] == 7);
	CHECK(mh_audio_decoder_seek(decoder, 44101, NULL) == MH_ERROR_NONE);
	CHECK(mh_audio_decoder_read(decoder, pcm, sizeof(pcm), 1, &count,
		NULL) == MH_ERROR_NONE &&
		count == 0);
	mh_audio_decoder_close(decoder);
	CHECK(fixture.opens == 1 && fixture.closes == 1);
	fixture.bad_info = 1;
	decoder = NULL;
	CHECK(mh_audio_decoder_open(&provider, resources, entry.path, &decoder,
		NULL) == MH_ERROR_INVALID);
	CHECK(decoder == NULL && fixture.opens == 2 && fixture.closes == 2);
	mh_memory_resources_close(resources);

	return 0;
}

static int
check_sample_loading(const struct mh_audio_decoder_provider *provider,
	const struct mh_resource_source *resources, struct fixture *fixture)
{
	struct mh_file storage;
	struct mh_audio_pcm pcm;
	unsigned char sentinel[1];
	size_t i;

	memset(&pcm, 0, sizeof(pcm));
	storage.data = sentinel;
	storage.size = 1;
	pcm.pcm = sentinel;
	CHECK(mh_audio_sample_load_from(provider, resources,
		"music/track-02.sample", 16, &storage, &pcm,
		NULL) == MH_ERROR_RANGE);
	CHECK(storage.data == sentinel && storage.size == 1 &&
		pcm.pcm == sentinel && pcm.frames == 0);
	CHECK(fixture->opens == 1 && fixture->closes == 1);
	fixture->fail = 1;
	CHECK(mh_audio_sample_load_from(provider, resources,
		"music/track-02.sample", MH_AUDIO_SAMPLE_BYTES_MAX,
		&storage, &pcm, NULL) == MH_ERROR_IO);
	CHECK(storage.data == sentinel && storage.size == 1 &&
		pcm.pcm == sentinel && pcm.frames == 0);
	CHECK(fixture->opens == 2 && fixture->closes == 2);
	fixture->fail = 0;
	CHECK(mh_audio_sample_load_from(provider, resources,
		"music/track-02.sample", MH_AUDIO_SAMPLE_BYTES_MAX,
		&storage, &pcm, NULL) == MH_ERROR_NONE);
	CHECK(fixture->opens == 3 && fixture->closes == 3);
	CHECK(pcm.pcm == storage.data && pcm.frames == 44101 &&
		pcm.bytes == storage.size && storage.size == 176404);
	CHECK(pcm.rate == 44100 && pcm.channels == 2 && pcm.bits == 16);

	/* All short reads were assembled, and PCM survives decoder close. */
	for (i = 0; i < storage.size; i++)
		CHECK(storage.data[i] == ((i / 4) & 255));

	free(storage.data);

	return 0;
}

static int
check_music_playback(const struct mh_audio_decoder_provider *provider,
	const struct mh_resource_source *resources, struct fixture *fixture)
{
	struct mh_music music;
	unsigned int opens;
	const unsigned char *pcm;
	float stereo[4];

	CHECK(mh_music_init_from(provider, resources, NULL, "music", 255, 1,
		&music, NULL) == MH_ERROR_NONE);
	CHECK(mh_music_play(&music, 2, 100, NULL) == MH_ERROR_NONE);
	CHECK(music.track == 2 && music.duration_ms == 1000 &&
		music.started_ms == 100);
	CHECK(fixture->opens == fixture->closes);
	CHECK(mh_audio_pcm_sample(&music.asset.wave, 1, 0) == 257.0 / 32768);
	CHECK(mh_mixer_render(&music.mixer, stereo, 2, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(stereo[0] == 0 && stereo[1] == 0 && stereo[2] > 0 &&
		stereo[2] == stereo[3]);
	opens = fixture->opens;
	pcm = music.asset.wave.pcm;
	CHECK(mh_music_poll(&music, 1100, NULL) == MH_ERROR_NONE);
	CHECK(music.started_ms == 100);
	CHECK(mh_music_poll(&music, 1101, NULL) == MH_ERROR_NONE);
	CHECK(music.started_ms == 1101 && music.mixer.voices[0].cursor == 0);
	CHECK(fixture->opens == opens && music.asset.wave.pcm == pcm);
	fixture->fail = 1;
	CHECK(mh_music_play(&music, 2, 1200, NULL) == MH_ERROR_IO);
	CHECK(music.track == 0 && music.asset.wave.pcm == NULL &&
		!music.mixer.voices[0].active);
	CHECK(fixture->opens == opens + 1 && fixture->opens == fixture->closes);
	fixture->fail = 0;
	CHECK(mh_music_play(&music, 2, 1300, NULL) == MH_ERROR_NONE);
	mh_music_stop(&music);
	CHECK(fixture->opens == fixture->closes);

	return 0;
}

static int
check_loading(void)
{
	static const struct mh_audio_decoder_ops ops = {sample_inspect,
		sample_open, sample_read, sample_seek, sample_close};

	struct mh_audio_decoder_provider provider;
	struct mh_resource_entry entry;
	const struct mh_resource_source *resources;
	struct fixture fixture;

	memset(&fixture, 0, sizeof(fixture));
	provider.resolve = NULL;
	provider.extension = "sample";
	provider.ops = &ops;
	provider.context = &fixture;
	entry.path = "music/track-02.sample";
	entry.data = (const unsigned char *)"fixture";
	entry.size = 7;
	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(check_sample_loading(&provider, resources, &fixture) == 0);
	CHECK(check_music_playback(&provider, resources, &fixture) == 0);
	mh_memory_resources_close(resources);

	return 0;
}

int
main(void)
{
	CHECK(check_wave() == 0);
	CHECK(check_mono() == 0);
	CHECK(check_selection() == 0);
	CHECK(check_loading() == 0);

	return 0;
}
