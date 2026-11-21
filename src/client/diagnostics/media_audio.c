#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/audio/sdl_provider.h"
#include "backends/clock/system_provider.h"
#include "backends/material/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/media_audio.h"
#include "engine/audio/menu_playback.h"
#include "engine/audio/race_playback.h"
#include "interfaces/audio/backend.h"
#include "interfaces/clock/backend.h"
#include "interfaces/storage/resource_backend.h"

struct mh_audio *
mh_audio_open(const char *root, const struct mh_track *track,
	const struct mh_audio_options *options, unsigned int trace)
{
	return mh_audio_open_with_clock(
		root, track, options, mh_system_clock_source(), trace);
}

struct mh_audio *
mh_audio_open_results(const char *root, const char *directory,
	unsigned int volume, unsigned int trace)
{
	return mh_audio_open_results_with_clock(
		root, directory, volume, mh_system_clock_source(), trace);
}

struct mh_menu_audio *
mh_menu_audio_open(const char *root, double volume)
{
	return mh_menu_audio_open_with_clock(
		root, volume, mh_system_clock_source());
}

struct mh_audio *
mh_audio_open_with_clock(const char *root, const struct mh_track *track,
	const struct mh_audio_options *options,
	const struct mh_clock_source *clock, unsigned int trace)
{
	return mh_audio_open_with_sources(
		root, track, options, mh_filesystem_resources(), clock, trace);
}

struct mh_audio *
mh_audio_open_results_with_clock(const char *root, const char *directory,
	unsigned int volume, const struct mh_clock_source *clock,
	unsigned int trace)
{
	return mh_audio_open_results_with_sources(root, directory, volume,
		mh_filesystem_resources(), clock, trace);
}

struct mh_menu_audio *
mh_menu_audio_open_with_clock(
	const char *root, double volume, const struct mh_clock_source *clock)
{
	return mh_menu_audio_open_with_sources(
		root, volume, mh_filesystem_resources(), clock);
}

struct mh_menu_audio *
mh_menu_audio_open_with_sources(const char *root, double volume,
	const struct mh_resource_source *resources,
	const struct mh_clock_source *clock)
{
	return mh_menu_audio_open_from(mh_sdl_audio_provider(),
		mh_audio_fallback_decoder(), root, volume, resources, clock);
}

struct mh_audio *
mh_audio_open_with_sources(const char *root, const struct mh_track *track,
	const struct mh_audio_options *options,
	const struct mh_resource_source *resources,
	const struct mh_clock_source *clock, unsigned int trace)
{
	return mh_audio_open_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_animation_lightwave_source(), mh_sdl_audio_provider(),
		mh_audio_fallback_decoder(), root, track, NULL, options,
		resources, clock, trace);
}

struct mh_audio *
mh_audio_open_results_with_sources(const char *root, const char *directory,
	unsigned int volume, const struct mh_resource_source *resources,
	const struct mh_clock_source *clock, unsigned int trace)
{
	return mh_audio_open_results_from(mh_sdl_audio_provider(),
		mh_audio_fallback_decoder(), root, directory, volume, resources,
		clock, trace);
}
