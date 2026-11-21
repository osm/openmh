#include <stdio.h>
#include <string.h>

#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/media_audio.h"
#include "engine/audio/menu_playback.h"
#include "engine/audio/race_playback.h"
#include "engine/catalog/load.h"
#include "engine/ui/pause.h"
#include "interfaces/audio/horn_source.h"
#include "interfaces/catalog/track.h"
#include "interfaces/clock/backend.h"
#include "interfaces/storage/resource_backend.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "audio clock line %d: %s\n", __LINE__, \
				#c);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	unsigned long	milliseconds;
	unsigned int	samples, reads;
	int		fail;
};

static struct mh_clock_counter
counter(void *context)
{
	struct fixture *fixture;
	struct mh_clock_counter value;

	fixture = context;
	fixture->samples++;
	value.low = fixture->milliseconds;
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
	struct fixture *fixture;

	fixture = context;

	return fixture->milliseconds;
}

static enum mh_error_code
read_resource(void *context, const char *path, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;
	fixture->reads++;

	if (fixture->fail)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"injected audio resource failure");

	return mh_resource_source_read_bounded(
		mh_filesystem_resources(), path, limit, output, error_out);
}

static enum mh_error_code
resource_size(void *context, const char *path, unsigned long *output,
	struct mh_error *error_out)
{
	(void)context;

	return mh_resource_source_size(
		mh_filesystem_resources(), path, output, error_out);
}

static enum mh_error_code
list_resources(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	(void)context;

	return mh_resource_source_list(
		mh_filesystem_resources(), path, visit, visitor, error_out);
}

int
main(int argc, char *argv[])
{
	static const struct mh_clock_ops ops = {
		counter, frequency, ticks, NULL, NULL};

	static const struct mh_resource_ops resource_ops = {
		read_resource, resource_size, list_resources, NULL, NULL};

	struct mh_resource_source race_resources, menu_resources;
	struct mh_horn_catalog horns;
	struct mh_clock_source race_clock, menu_clock;
	struct fixture race_fixture, menu_fixture;
	struct mh_audio *race, *results;
	struct mh_menu_audio *menu;
	struct mh_audio_options options;
	struct mh_audio_music_state state;
	struct mh_track track;
	struct mh_pause_output pause;
	unsigned int samples, race_reads, menu_reads;

	CHECK(argc == 3);
	memset(&race_fixture, 0, sizeof(race_fixture));
	memset(&menu_fixture, 0, sizeof(menu_fixture));
	memset(&options, 0, sizeof(options));
	memset(&track, 0, sizeof(track));
	memset(&pause, 0, sizeof(pause));
	race_fixture.milliseconds = 1000;
	menu_fixture.milliseconds = 7000;
	CHECK(mh_clock_source_init(&race_clock, &ops, &race_fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_clock_source_init(&menu_clock, &ops, &menu_fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_resource_source_init(&race_resources, &resource_ops,
		&race_fixture, NULL) == MH_ERROR_NONE);
	CHECK(mh_resource_source_init(&menu_resources, &resource_ops,
		&menu_fixture, NULL) == MH_ERROR_NONE);
	CHECK(mh_track_config_load_from(mh_catalog_fallback_source(),
		mh_filesystem_resources(), argv[1], 1, &track,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_horn_catalog_load_from(mh_horn_fallback_source(),
		mh_filesystem_resources(), argv[1], &horns,
		NULL) == MH_ERROR_NONE);
	options.music_volume = 1;
	options.music_root = argv[2];
	options.no_scenery = 1;
	track.cd_track = 2;

	CHECK(mh_audio_open_with_clock(argv[1], &track, &options, NULL, 0) ==
		NULL);
	CHECK(mh_menu_audio_open_with_clock(argv[1], 0, NULL) == NULL);
	CHECK(mh_audio_open_results_with_clock(
		argv[1], argv[2], 255, NULL, 0) == NULL);
	CHECK(mh_audio_open_with_sources(
		argv[1], &track, &options, NULL, &race_clock, 0) == NULL);
	CHECK(mh_menu_audio_open_with_sources(argv[1], 0, NULL, &menu_clock) ==
		NULL);
	CHECK(mh_audio_open_results_with_sources(
		argv[1], argv[2], 255, NULL, &menu_clock, 0) == NULL);
	CHECK(race_fixture.samples == 0 && menu_fixture.samples == 0 &&
		race_fixture.reads == 0 && menu_fixture.reads == 0);
	CHECK(mh_audio_update(NULL, NULL, 0, 0, 0, 0));

	race = mh_audio_open_with_sources(
		argv[1], &track, &options, &race_resources, &race_clock, 0);
	results = mh_audio_open_results_with_sources(
		argv[1], argv[2], 255, &menu_resources, &menu_clock, 0);
	CHECK(race != NULL && results != NULL);
	CHECK(mh_audio_music_inspect(race, &state) && state.started_ms == 1000);
	CHECK(mh_audio_music_inspect(results, &state) &&
		state.started_ms == 7000);
	CHECK(race_fixture.samples == 1 && menu_fixture.samples == 1 &&
		race_fixture.reads > 1 && menu_fixture.reads == 1);

	/*
	 * Volume edits retain the song clock; results sample the same source.
	 */
	pause.count = 1;
	pause.events[0].kind = MH_PAUSE_CD_VOLUME;
	pause.events[0].value = 0;
	CHECK(mh_audio_menu(race, &pause, 1));
	race_fixture.milliseconds = 3000;
	pause.events[0].value = MH_PAUSE_VOLUME_MAX;
	CHECK(mh_audio_menu(race, &pause, 1));
	CHECK(mh_audio_music_inspect(race, &state) && state.started_ms == 1000);
	CHECK(mh_audio_results(race, 0));
	CHECK(mh_audio_music_inspect(race, &state) && state.started_ms == 3000);
	CHECK(mh_audio_music_inspect(results, &state) &&
		state.started_ms == 7000);
	CHECK(menu_fixture.samples == 1);

	race_reads = race_fixture.reads;
	menu_reads = menu_fixture.reads;
	menu = mh_menu_audio_open_with_sources(
		argv[1], 0, &menu_resources, &menu_clock);
	CHECK(menu != NULL && race_fixture.reads == race_reads &&
		menu_fixture.reads > menu_reads);
	menu_reads = menu_fixture.reads;
	CHECK(mh_menu_audio_load_horns(menu, argv[1], &horns));
	CHECK(race_fixture.reads == race_reads &&
		menu_fixture.reads > menu_reads);
	samples = race_fixture.samples;
	menu_fixture.milliseconds = 9000;
	mh_menu_audio_music(menu, argv[1], argv[2], 255, 2);
	CHECK(mh_menu_audio_current_track(menu) == 2 &&
		menu_fixture.samples == 2);
	mh_menu_audio_music_stop(menu);
	mh_menu_audio_music_play(menu, 2);
	CHECK(mh_menu_audio_current_track(menu) == 2 &&
		menu_fixture.samples == 3);
	CHECK(race_fixture.samples == samples);
	menu_fixture.fail = 1;
	mh_menu_audio_music_play(menu, 2);
	CHECK(mh_menu_audio_current_track(menu) == 0 &&
		menu_fixture.samples == 3);
	CHECK(mh_audio_music_inspect(results, &state) &&
		state.started_ms == 7000);
	CHECK(race_fixture.reads == race_reads &&
		race_fixture.samples == samples);
	menu_fixture.fail = 0;
	mh_menu_audio_music_play(menu, 2);
	CHECK(mh_menu_audio_current_track(menu) == 2 &&
		menu_fixture.samples == 4);
	race_reads = race_fixture.reads;
	menu_reads = menu_fixture.reads;
	mh_menu_audio_close(menu);
	mh_audio_close(results);
	mh_audio_close(race);
	CHECK(race_fixture.samples == samples && menu_fixture.samples == 4);
	CHECK(race_fixture.reads == race_reads &&
		menu_fixture.reads == menu_reads);
	puts("Race, results and menu audio retain independent clock/resource providers");

	return 0;
}
