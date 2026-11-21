#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/wave_provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/audio/music.h"
#include "engine/audio/music_catalog.h"
#include "engine/audio/pcm.h"
#include "interfaces/clock/backend.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			return 1;                                              \
		}                                                              \
	} while (0)

struct clock_state {
	struct mh_music	*music;
	unsigned int	queries, loaded;
};

struct catalog_fixture {
	unsigned char	header[44];
	size_t		read_bytes;
};

static struct mh_clock_counter
counter(void *context)
{
	struct clock_state *clock;
	struct mh_clock_counter value;

	clock = context;
	clock->queries++;
	clock->loaded = clock->music->track == 2 &&
		clock->music->asset.wave.pcm != NULL;
	value.low = 750;
	value.high = 0;

	return value;
}

static struct mh_clock_counter
frequency(void *context)
{
	struct mh_clock_counter value;

	(void)context;
	value.low = 1000;
	value.high = 0;

	return value;
}

static unsigned long
ticks(void *context)
{
	(void)context;

	return 750;
}

static enum mh_error_code
reject_full_read(void *context, const char *path, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	(void)context;

	(void)limit;
	(void)path;
	(void)output;

	return mh_error_set(
		error_out, MH_ERROR_FAILED, "catalog attempted to load PCM");
}

static enum mh_error_code
catalog_size(void *context, const char *path, unsigned long *output,
	struct mh_error *error_out)
{
	(void)context;

	if (strcmp(path, "music/track-02.wav") != 0)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "missing track");

	*output = 176444;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
catalog_list(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	(void)context;

	if (strcmp(path, "music") != 0)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"missing music directory");

	if (visit(visitor, "track-02.wav") == MH_DIRECTORY_FAIL)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "catalog visitor failed");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
catalog_range(void *context, const char *path, unsigned long offset,
	void *output, size_t size, struct mh_error *error_out)
{
	struct catalog_fixture *fixture;

	fixture = context;

	if (strcmp(path, "music/track-02.wav") != 0 ||
		offset > sizeof(fixture->header) ||
		size > sizeof(fixture->header) - offset)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "catalog read PCM payload");

	memcpy(output, fixture->header + offset, size);
	fixture->read_bytes += size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
check_catalog(const unsigned char *wave)
{
	struct mh_resource_ops ops = {reject_full_read, catalog_size,
		catalog_list, catalog_range, NULL};

	struct mh_resource_source source, unavailable;
	struct mh_resource_ops limited;
	struct catalog_fixture fixture;
	struct mh_music_catalog catalog, before;

	memcpy(fixture.header, wave, sizeof(fixture.header));

	/*
	 * One second of advertised PCM; only its metadata exists in this
	 * provider.
	 */
	fixture.header[4] = 0x34;
	fixture.header[5] = 0xb1;
	fixture.header[6] = 2;
	fixture.header[40] = 0x10;
	fixture.header[41] = 0xb1;
	fixture.header[42] = 2;
	fixture.read_bytes = 0;
	memset(&catalog, 0, sizeof(catalog));
	catalog.seconds[8] = 99;
	CHECK(mh_resource_source_init(&source, &ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_music_catalog_scan_from(mh_audio_wave_decoder(), &source, NULL,
		"music", &catalog, NULL) == MH_ERROR_NONE);
	CHECK(catalog.last_track == 2 && catalog.seconds[2] == 1);
	CHECK(catalog.seconds[8] == 99 && fixture.read_bytes == 44);
	before = catalog;
	limited = ops;
	limited.read_at = NULL;
	CHECK(mh_resource_source_init(&unavailable, &limited, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_music_catalog_scan_from(mh_audio_wave_decoder(), &unavailable,
		NULL, "music", &catalog, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&catalog, &before, sizeof(catalog)) == 0);

	return 0;
}

int
main(void)
{
	/* Two CD frames: +0.5 in each channel, followed by silence. */
	static const unsigned char wave[] = {'R', 'I', 'F', 'F', 44, 0, 0, 0,
		'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
		0x44, 0xac, 0, 0, 0x10, 0xb1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't',
		'a', 8, 0, 0, 0, 0, 0x40, 0, 0x40, 0, 0, 0, 0};

	static const struct mh_clock_ops clock_ops = {
		counter, frequency, ticks, NULL, NULL};

	struct mh_resource_entry entry;
	const struct mh_resource_source *first_source, *second_source;
	struct mh_music first, second;
	struct mh_clock_source clock;
	struct clock_state state;
	unsigned char second_wave[sizeof(wave)];
	unsigned int queries;

	CHECK(check_catalog(wave) == 0);
	memcpy(second_wave, wave, sizeof(second_wave));
	second_wave[45] = second_wave[47] = 0xc0;
	entry.path = "music/track-02.wav";
	entry.data = wave;
	entry.size = sizeof(wave);
	CHECK(mh_memory_resources_open(&entry, 1, &first_source, NULL) ==
		MH_ERROR_NONE);
	entry.data = second_wave;
	CHECK(mh_memory_resources_open(&entry, 1, &second_source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_music_init_from(mh_audio_wave_decoder(), first_source, NULL,
		"music", 255, 0, &first, NULL) == MH_ERROR_NONE);
	CHECK(mh_music_init_from(mh_audio_wave_decoder(), second_source, NULL,
		"music", 128, 0, &second, NULL) == MH_ERROR_NONE);
	memset(&state, 0, sizeof(state));
	state.music = &first;
	CHECK(mh_clock_source_init(&clock, &clock_ops, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_music_play_clocked(&first, 2, &clock, NULL) == MH_ERROR_NONE);
	CHECK(state.loaded && state.queries == 1 && first.started_ms == 750);
	CHECK(mh_music_play(&second, 2, 900, NULL) == MH_ERROR_NONE);
	CHECK(second.started_ms == 900 && second.volume == 128);
	CHECK(mh_audio_pcm_sample(&first.asset.wave, 0, 0) == .5);
	CHECK(mh_audio_pcm_sample(&second.asset.wave, 0, 0) == -.5);
	queries = state.queries;
	CHECK(mh_music_play_clocked(&first, 3, &clock, NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(first.track == 0 && first.asset.wave.pcm == NULL &&
		state.queries == queries);
	CHECK(second.track == 2 &&
		mh_audio_pcm_sample(&second.asset.wave, 0, 1) == -.5);
	mh_music_stop(&first);
	mh_music_stop(&second);
	mh_memory_resources_close(first_source);
	mh_memory_resources_close(second_source);
	puts("Music owners retain separate resources and sample clocks after PCM loading");

	return 0;
}
