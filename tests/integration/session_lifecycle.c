#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/collision/provider.h"
#include "backends/font/provider.h"
#include "backends/geometry/lob_provider.h"
#include "backends/geometry/myo_provider.h"
#include "backends/geometry/myw_provider.h"
#include "backends/image/act_provider.h"
#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/image/remap_provider.h"
#include "backends/image/tga_provider.h"
#include "backends/material/provider.h"
#include "backends/navigation/provider.h"
#include "backends/retro/provider.h"
#include "backends/sprite/spr_provider.h"
#include "backends/ui/dta_provider.h"
/*
 * Installed-asset check; all recording files belong to the caller's scratch
 * dir.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/render_renderer.h"
#include "client/session/attract_session.h"
#include "client/session/demo_session.h"
#include "client/session/league_setup.h"
#include "client/session/loading.h"
#include "client/session/network_setup.h"
#include "client/session/single_session.h"
#include "client/settings/render_choice.h"
#include "interfaces/audio/backend.h"
#include "interfaces/input/backend.h"
#include "interfaces/render/backend.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/window/backend.h"
#ifdef MH_TEST_NETWORK
#include "client/session/lobby.h"
#endif
#include "backends/audio/sdl_provider.h"
#include "backends/input/sdl_provider.h"
#include "backends/render/sdl_canvas_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_user_data.h"
#include "backends/window/sdl_provider.h"
#include "engine/catalog/load.h"
#include "engine/clock/game_timer.h"
#include "engine/core/error.h"
#include "engine/storage/record_file.h"
#include "interfaces/audio/horn_source.h"
#include "interfaces/clock/backend.h"
#include "interfaces/storage/directory.h"
#include "interfaces/storage/resource_backend.h"
#include "interfaces/storage/user_data_backend.h"
#include "interfaces/window/window.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "session lifecycle line %d: %s\n",     \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

#define LOADING_FRAMES 7

static struct mh_profile_catalog catalog;

/* Observe real backend calls without replacing presentation or asset loads. */
static struct {
	const struct mh_render_backend	*backend;
	struct mh_renderer		*renderer;
	unsigned long			window;
	unsigned int			opens, closes, frames, asset_reads;
	unsigned int			reads_at_frame[LOADING_FRAMES];
	unsigned int			fail_frame, history_requests;
	int				fail_history, fail_race;
	char				failure_text[128];
	int				enabled, handoff, capture;
	char				record_key[1024];
} loading;

struct resource_fixture {
	const char	*root;
	unsigned int	reads, fail_at;
};

struct record_fixture {
	const struct mh_user_data	*data;
	unsigned int			reads, writes, roots;
};

struct clock_fixture {
	unsigned long	now;
	unsigned int	samples;
};

static enum mh_error_code
set_blur_history(struct mh_renderer *renderer, unsigned int frames,
	struct mh_error *error_out)
{
	loading.history_requests++;

	if (frames == 3 && loading.fail_history)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"fixture history allocation failure");

	return loading.backend->ops->set_blur_history(
		renderer, frames, error_out);
}

static enum mh_error_code
open_renderer(const void *context, struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	(void)flags;
	(void)context;
	status = loading.backend->open(loading.backend->context, window, meshes,
		count, width, height, flags, output, error_out);

	if (status == MH_ERROR_NONE && loading.enabled) {
		loading.opens++;

		if (count == 0) {
			loading.renderer = *output;
			loading.window = mh_window_id(window);
		} else
			loading.handoff = loading.renderer == NULL &&
				loading.window == mh_window_id(window);
	}

	return status;
}

static int
render_frame(struct mh_renderer *renderer, const float matrix[16],
	const float view[16], const float *models, int reflections,
	const struct mh_ui_list *ui, const struct mh_retro_draw *retro,
	struct mh_error *error_out)
{
	int ok;
	const char *path;
	char capture[1024], suffix[16];
	size_t length;

	if (loading.fail_race && renderer != loading.renderer) {
		strcpy(loading.failure_text,
			"injected race presentation failure");
		mh_error_set(error_out, MH_ERROR_BACKEND, loading.failure_text);
		return 0;
	}

	if (loading.enabled && renderer == loading.renderer &&
		loading.frames + 1 == loading.fail_frame) {
		mh_error_set(error_out, MH_ERROR_BACKEND,
			"injected loading presentation failure");
		return 0;
	}

	ok = loading.backend->ops->scene_frame_retro(renderer, matrix, view,
		models, reflections, ui, retro, error_out);

	if (ok && loading.enabled && renderer == loading.renderer) {
		if (loading.frames >= LOADING_FRAMES) {
			mh_error_set(error_out, MH_ERROR_STATE,
				"too many loading frames");
			return 0;
		}

		loading.reads_at_frame[loading.frames] = loading.asset_reads;
		loading.frames++;
		path = loading.capture ? getenv("MH_TEST_LOADING_CAPTURE")
				: NULL;

		if (path != NULL) {
			length = strlen(path);

			if (length > sizeof(capture) - sizeof(suffix)) {
				mh_error_set(error_out, MH_ERROR_RANGE,
					"loading capture path too long");
				return 0;
			}

			sprintf(suffix, ".%u.bmp", loading.frames);
			memcpy(capture, path, length);
			strcpy(capture + length, suffix);

			if (!mh_renderer_capture(renderer, capture)) {
				mh_renderer_error(renderer, error_out);
				return 0;
			}
		}
	}

	return ok;
}

static enum mh_error_code
render_lens(struct mh_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro,
	const struct mh_renderer_effects *effects, struct mh_error *error_out)
{
	if (loading.fail_race && renderer != loading.renderer) {
		strcpy(loading.failure_text,
			"injected race presentation failure");
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, loading.failure_text);
	}

	return loading.backend->ops->scene_frame_lens(renderer, projection,
		view, models, reflections, ui, retro, effects, error_out);
}

static void
destroy_renderer(struct mh_renderer *renderer)
{
	if (loading.enabled) {
		loading.closes++;

		if (renderer == loading.renderer)
			loading.renderer = NULL;
	}

	strcpy(loading.failure_text, "renderer has been destroyed");
	loading.backend->ops->destroy(renderer);
}

static void
observe_loading(int capture)
{
	loading.enabled = 1;
	loading.capture = capture;
	loading.record_key[0] = 0;
	loading.opens = loading.closes = loading.frames = 0;
	loading.asset_reads = 0;
	loading.fail_frame = 0;
	loading.fail_race = 0;
	loading.fail_history = 0;
	loading.history_requests = 0;
	memset(loading.reads_at_frame, 0, sizeof(loading.reads_at_frame));
	loading.window = 0;
	loading.handoff = 0;
}

static int
check_loading_frames(unsigned int renderer, int ai)
{
	unsigned int i, count;

	count = renderer == MH_RENDER_CHOICE_SOFTWARE ? LOADING_FRAMES : 1;
	CHECK(loading.frames == count && loading.asset_reads > 0);
	CHECK(loading.reads_at_frame[0] == 0);

	if (renderer == MH_RENDER_CHOICE_SOFTWARE) {
		CHECK(loading.reads_at_frame[1] == 0);

		for (i = 2; i < count; i++) {
			if (i == 5 && !ai)
				CHECK(loading.reads_at_frame[i] ==
					loading.reads_at_frame[i - 1]);
			else
				CHECK(loading.reads_at_frame[i] >
					loading.reads_at_frame[i - 1]);
		}
	}

	return 0;
}

static int
check_loading_records(const char *track, unsigned int laps)
{
	char name[300], suffix[16];
	const char *key;

	sprintf(suffix, "%02u.CR", laps);
	CHECK(strlen(track) + strlen(suffix) < sizeof(name));
	strcpy(name, track);
	strcat(name, suffix);
	key = strrchr(loading.record_key, '/');
	CHECK(strcmp(key == NULL ? loading.record_key : key + 1, name) == 0);

	return 0;
}

/* Logical asset names cannot accidentally resolve through native readers. */
#define RESOURCE_ROOT "session-assets"
#define RESOURCE_PATH_SIZE 1024

static enum mh_error_code
read_records(void *context, const char *key, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	struct record_fixture *fixture;

	fixture = context;
	fixture->reads++;

	if (loading.enabled && loading.frames == 0) {
		if (strlen(key) >= sizeof(loading.record_key))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"fixture record key too long");

		strcpy(loading.record_key, key);
	}

	return mh_user_data_read(fixture->data, key, limit, output, error_out);
}

static enum mh_error_code
write_records(void *context, const char *key, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct record_fixture *fixture;

	fixture = context;
	fixture->writes++;

	return mh_user_data_replace(fixture->data, key, bytes, size, error_out);
}

static enum mh_error_code
record_directory(void *context, char **output, struct mh_error *error_out)
{
	struct record_fixture *fixture;

	fixture = context;
	fixture->roots++;

	return mh_user_data_directory(fixture->data, output, error_out);
}

static enum mh_error_code
check_record_directory(
	void *context, const char *path, struct mh_error *error_out)
{
	struct record_fixture *fixture;

	fixture = context;

	return mh_user_data_check_directory(fixture->data, path, error_out);
}

static enum mh_error_code
list_records(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	struct record_fixture *fixture;

	fixture = context;

	return mh_user_data_list(
		fixture->data, path, visit, visitor, error_out);
}

static enum mh_error_code
create_record_directory(
	void *context, const char *path, struct mh_error *error_out)
{
	struct record_fixture *fixture;

	fixture = context;

	return mh_user_data_create_directory(fixture->data, path, error_out);
}

static enum mh_error_code
open_record_directory(void *context, const char *path, const char *root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	struct record_fixture *fixture;

	fixture = context;

	return mh_user_data_open_directory(
		fixture->data, path, root, output, error_out);
}

static enum mh_directory_action
count_recordings(void *context, const char *name)
{
	unsigned int *count;

	(void)name;
	count = context;
	(*count)++;

	return MH_DIRECTORY_CONTINUE;
}

/* The input provider is gone before playback takes ownership of its copy. */
static int
load_memory_demo(const struct mh_resource_source *resources, const char *path,
	struct mh_file *output)
{
	const struct mh_user_data *data;
	struct mh_file original;

	CHECK(mh_resource_source_read(resources, path, &original, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_replace(data, "saved.MDE", original.data,
		original.size, NULL) == MH_ERROR_NONE);
	free(original.data);
	CHECK(mh_user_data_read(data, "saved.MDE", MH_RESOURCE_READ_LIMIT,
		output, NULL) == MH_ERROR_NONE);
	mh_memory_user_data_close(data);

	return 0;
}

static enum mh_error_code
resolve_resource_path(struct resource_fixture *fixture, const char *path,
	char output[RESOURCE_PATH_SIZE], struct mh_error *error_out)
{
	size_t prefix;

	prefix = sizeof(RESOURCE_ROOT) - 1;

	if (strncmp(path, RESOURCE_ROOT, prefix) != 0 ||
		(path[prefix] != 0 && path[prefix] != '/'))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"resource outside session namespace");

	if (strlen(fixture->root) + strlen(path + prefix) + 1 >
		RESOURCE_PATH_SIZE)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"session fixture path too long");

	strcpy(output, fixture->root);
	strcat(output, path + prefix);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_resource(void *context, const char *path, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	struct resource_fixture *fixture;
	char native[RESOURCE_PATH_SIZE];
	enum mh_error_code status;

	fixture = context;
	fixture->reads++;

	if (fixture->reads == fixture->fail_at)
		return mh_error_set(error_out, MH_ERROR_IO,
			"injected session resource failure");

	if (loading.enabled && loading.frames != 0 && loading.renderer != NULL)
		loading.asset_reads++;

	status = resolve_resource_path(fixture, path, native, error_out);

	return status == MH_ERROR_NONE
		? mh_resource_source_read_bounded(mh_filesystem_resources(),
			native, limit, output, error_out)
		: status;
}

static enum mh_error_code
resource_size(void *context, const char *path, unsigned long *output,
	struct mh_error *error_out)
{
	char native[RESOURCE_PATH_SIZE];
	enum mh_error_code status;

	status = resolve_resource_path(context, path, native, error_out);

	return status == MH_ERROR_NONE
		? mh_resource_source_size(
			mh_filesystem_resources(), native, output, error_out)
		: status;
}

static enum mh_error_code
list_resources(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	char native[RESOURCE_PATH_SIZE];
	enum mh_error_code status;

	status = resolve_resource_path(context, path, native, error_out);

	return status == MH_ERROR_NONE
		? mh_resource_source_list(mh_filesystem_resources(), native,
			visit, visitor, error_out)
		: status;
}

static struct mh_clock_counter
read_counter(void *context)
{
	struct clock_fixture *fixture;
	struct mh_clock_counter value;

	fixture = context;
	fixture->samples++;
	fixture->now += 17;
	value.low = fixture->now;
	value.high = 0;

	return value;
}

static struct mh_clock_counter
read_frequency(void *context)
{
	struct mh_clock_counter value;

	(void)context;
	value.low = 1000;
	value.high = 0;

	return value;
}

static unsigned long
read_ticks(void *context)
{
	struct clock_fixture *fixture;

	fixture = context;

	return fixture->now;
}

static enum mh_directory_action
found_recording(void *context, const char *name)
{
	int *found;

	(void)name;
	found = (int *)context;
	*found = 1;

	return MH_DIRECTORY_STOP;
}

static int
directory_empty(const char *path)
{
	int found;

	found = 0;

	return mh_resource_source_list(mh_filesystem_resources(), path,
		found_recording, &found, NULL) == MH_ERROR_NONE &&
		!found;
}

static int
check_league_assets(const struct mh_resource_source *resources,
	struct mh_settings *settings,
	const struct mh_session_options *session_options)
{
	static struct mh_race_context context;
	static struct mh_field field;
	struct mh_league_document document;
	struct mh_league_asset_selection selection;
	struct mh_league_drive_options options;
	struct mh_session_loading presentation;
	enum mh_error_code status;
	unsigned long random;

	random = 1;
	CHECK(mh_league_create(catalog.profiles, catalog.count, "Resource test",
		"Player", "ASC", 0xffcc88UL, 123, &random, &document,
		NULL) == MH_ERROR_NONE);
	memset(&options, 0, sizeof(options));
	options.car = 1;
	options.horn = "Honker1";
	options.difficulty = 2;
	mh_session_loading_init(&presentation, RESOURCE_ROOT, session_options);
	observe_loading(0);
	status = mh_league_setup_load_presented_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_catalog_fallback_source(), mh_navigation_mot_dat_source(),
		mh_animation_lightwave_source(), mh_atlas_remap_source(),
		mh_model_myo_decoder(), mh_world_myw_decoder(),
		mh_collision_fallback_source(), mh_sprite_spr_source(),
		mh_font_fallback_source(), session_options->image_source,
		session_options->image_decoder,
		session_options->portrait_decoder, resources, RESOURCE_ROOT,
		&document, &options, settings, &context, &field, &selection,
		&presentation, NULL, NULL);
	CHECK(status == MH_ERROR_NONE);
	CHECK(context.resources == resources && field.count == 8);
	CHECK(mh_league_setup_ui_presented(RESOURCE_ROOT, &document, &selection,
		settings, &context, &presentation,
		NULL) == MH_ERROR_NONE);
	CHECK(check_loading_frames(settings->renderer, 1) == 0);
	CHECK(check_loading_records(context.scene.assets.track.name,
		context.scene.assets.track.laps) == 0);
	mh_session_loading_close(&presentation);
	CHECK(loading.opens == 1 && loading.closes == 1);
	loading.enabled = 0;
	mh_race_context_free(&context);

	return 0;
}

static int
check_network_assets(const struct mh_resource_source *resources,
	struct mh_settings *settings, struct mh_game_timer *timer,
	const struct mh_session_options *session_options)
{
	static struct mh_race_context context;
	static struct mh_net_session session;
	static struct mh_net_race race;
	struct mh_net_session_config config;
	struct mh_session_loading presentation;
	enum mh_error_code status;
	struct mh_track track;
	struct mh_car car;
#ifdef MH_TEST_NETWORK
	struct mh_network_lobby *lobby;
	unsigned int host;
#endif

	(void)timer;
	CHECK(mh_track_config_load_from(mh_catalog_fallback_source(), resources,
		RESOURCE_ROOT, 1, &track, NULL) == MH_ERROR_NONE);
	CHECK(mh_car_config_load_from(mh_catalog_fallback_source(), resources,
		RESOURCE_ROOT, 1, &car, NULL) == MH_ERROR_NONE);
	memset(&config, 0, sizeof(config));
	config.provider = 2;
	config.laps = 3;
	config.car_count = 10;
	strcpy(config.track, track.name);
	strcpy(config.profile.car, car.name);
	strcpy(config.profile.name, "Driver");
	strcpy(config.profile.horn, "Honker1");
	config.profile.update_rate = 15;
	CHECK(mh_net_session_init(1, &config, &session, NULL) == MH_ERROR_NONE);
	CHECK(mh_network_setup_bind_cars_from(mh_catalog_fallback_source(),
		resources, RESOURCE_ROOT, &session,
		NULL) == MH_ERROR_NONE);
	session.phase = MH_NET_LOADING;
	session.player = 0;
	session.roster.player_count = 1;
	session.roster.laps = 3;
	strcpy(session.roster.track, track.name);
	session.roster.players[0] = config.profile;
	session.roster.players[0].flags = 1;
	session.roster.players[0].value_a = 1;
	mh_session_loading_init(&presentation, RESOURCE_ROOT, session_options);
	observe_loading(0);
	status = mh_network_setup_load_presented_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_catalog_fallback_source(), mh_navigation_mot_dat_source(),
		mh_animation_lightwave_source(), mh_atlas_remap_source(),
		mh_model_myo_decoder(), mh_world_myw_decoder(),
		mh_collision_fallback_source(), mh_sprite_spr_source(),
		mh_font_fallback_source(), session_options->image_source,
		session_options->image_decoder,
		session_options->portrait_decoder, resources, RESOURCE_ROOT,
		&session, 0, settings, &context, &race, &presentation, NULL,
		NULL);
	CHECK(status == MH_ERROR_NONE);
	CHECK(context.resources == resources && race.field.count == 1);
	CHECK(mh_network_setup_ui_presented(RESOURCE_ROOT, &session, &race,
		&context, &presentation, NULL) == MH_ERROR_NONE);
	CHECK(check_loading_frames(settings->renderer, 0) == 0);
	CHECK(check_loading_records(track.name, 3) == 0);
	mh_session_loading_close(&presentation);
	CHECK(loading.opens == 1 && loading.closes == 1);
	loading.enabled = 0;
	mh_race_context_free(&context);

#ifdef MH_TEST_NETWORK
	for (host = 0; host < 2; host++) {
		lobby = NULL;
		CHECK(mh_network_lobby_open_from(mh_horn_fallback_source(),
			mh_catalog_fallback_source(),
			mh_font_fallback_source(),
			mh_image_fallback_decoder(), resources,
			RESOURCE_ROOT, host, timer,
			mh_sdl_window_system(), NULL,
			mh_sdl_canvas_provider(), mh_sdl_input_provider(),
			NULL, NULL, NULL, NULL, NULL, NULL, &lobby,
			NULL) == MH_ERROR_NONE);
		mh_network_lobby_close(lobby);
	}
#endif

	return 0;
}

static int
check_sessions(const char *root, const char *recordings, unsigned int renderer,
	int shared_window)
{
	static const struct mh_clock_ops clock_ops = {
		read_counter, read_frequency, read_ticks, NULL, NULL};

	static const struct mh_resource_ops resource_ops = {
		read_resource, resource_size, list_resources, NULL, NULL};

	static const struct mh_user_data_ops data_ops = {read_records,
		write_records, record_directory, list_records,
		create_record_directory, open_record_directory,
		check_record_directory, NULL};

	struct record_fixture record_state;
	struct mh_user_data data;
	struct resource_fixture resource_state;
	struct mh_resource_source resources;
	struct clock_fixture clock_state;
	struct mh_clock_source clock;
	struct mh_game_timer timer;
	struct mh_horn_catalog horns;
	struct mh_settings settings;
	struct mh_session_options options;
	struct mh_render_set renderers;
	struct mh_render_backend backend;
	struct mh_render_ops render_ops;
	struct mh_activity activity;
	struct mh_demo_file *take;
	struct mh_file bytes, held;
	struct mh_track track;
	struct mh_records records;
	struct mh_error error;
	enum mh_error_code status;
	char previous[16], demo[1024];
	unsigned long random;
	unsigned int mode, frames, samples, reads, record_reads, roots, count;
	int quit, manual_exit;

	CHECK(directory_empty(recordings));
	random = 1;
	memset(previous, 0, sizeof(previous));
	memset(&options, 0, sizeof(options));
	memset(&clock_state, 0, sizeof(clock_state));
	CHECK(mh_clock_source_init(&clock, &clock_ops, &clock_state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_game_timer_init(&timer, &clock, NULL) == MH_ERROR_NONE);
	options.canvases = mh_sdl_canvas_provider();
	options.renderers = mh_diagnostic_renderers();
	renderers = *options.renderers;

	if (renderer == MH_RENDER_CHOICE_SOFTWARE)
		loading.backend = renderers.software;
	else if (renderer == MH_RENDER_CHOICE_DIRECT3D)
		loading.backend = renderers.direct3d;
	else if (renderer == MH_RENDER_CHOICE_POWERVR)
		loading.backend = renderers.powervr;
	else
		loading.backend = renderers.glide;

	backend = *loading.backend;
	render_ops = *backend.ops;

	if (renderer == MH_RENDER_CHOICE_DIRECT3D)
		render_ops.set_blur_history = set_blur_history;

	render_ops.scene_frame_retro = render_frame;

	if (render_ops.scene_frame_lens != NULL)
		render_ops.scene_frame_lens = render_lens;

	render_ops.destroy = destroy_renderer;
	backend.open = open_renderer;
	backend.ops = &render_ops;

	if (renderer == MH_RENDER_CHOICE_SOFTWARE)
		renderers.software = &backend;
	else if (renderer == MH_RENDER_CHOICE_DIRECT3D)
		renderers.direct3d = &backend;
	else if (renderer == MH_RENDER_CHOICE_POWERVR)
		renderers.powervr = &backend;
	else
		renderers.glide = &backend;

	options.renderers = &renderers;
	options.windows = mh_sdl_window_system();

	if (shared_window) {
		CHECK(mh_window_system_acquire(options.windows, NULL, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_window_open(options.windows, "Shared session window",
			800, 600, MH_WINDOW_RESIZABLE, &options.window,
			NULL) == MH_ERROR_NONE);
	}

	options.inputs = mh_sdl_input_provider();
	options.audio_provider = mh_sdl_audio_provider();
	options.image_source = mh_image_pdi_source();
	options.font_source = mh_font_fallback_source();
	options.wireframe_source = mh_wireframe_lob_source();
	options.sprite_source = mh_sprite_spr_source();
	options.model_decoder = mh_model_myo_decoder();
	options.world_decoder = mh_world_myw_decoder();
	options.atlas_source = mh_atlas_remap_source();
	options.animation_source = mh_animation_lightwave_source();
	options.navigation_source = mh_navigation_mot_dat_source();
	options.catalog_source = mh_catalog_fallback_source();
	options.profile_source = mh_profile_fallback_source();
	options.palette_source = mh_palette_act_source();
	options.layout_source = mh_menu_layout_dta_source();
	options.material_source = mh_material_mat_srf_source();
	options.env_sound_source = mh_env_sounds_esd_source();
	options.horn_source = mh_horn_fallback_source();
	options.collision_source = mh_collision_fallback_source();
	options.image_decoder = mh_image_fallback_decoder();
	options.portrait_decoder = mh_image_tga_portrait_decoder();
	options.retro_source = mh_retro_fallback_source();
	options.audio_decoder = mh_audio_fallback_decoder();
	options.audio_trace = 0;
	options.timer = &timer;
	memset(&resource_state, 0, sizeof(resource_state));
	resource_state.root = root;
	CHECK(mh_resource_source_init(&resources, &resource_ops,
		&resource_state, NULL) == MH_ERROR_NONE);
	options.resources = &resources;
	memset(&record_state, 0, sizeof(record_state));
	CHECK(mh_memory_user_data_open(&record_state.data, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_init(&data, &data_ops, &record_state, NULL) ==
		MH_ERROR_NONE);
	options.user_data = &data;
	options.screenshot_data = &data;
	options.screenshot_encoder = mh_image_tga_encoder();
	options.recording_directory = "Recordings";

	/* Distinct existing records must be displayed before geometry loads. */
	CHECK(mh_track_config_load_from(mh_catalog_fallback_source(),
		&resources, RESOURCE_ROOT, 1, &track,
		NULL) == MH_ERROR_NONE);
	memset(&records, 0, sizeof(records));
	records.has_lap = records.has_race = 1;
	records.lap.milliseconds = 61239;
	records.race.milliseconds = 185678;
	CHECK(mh_record_file_save_to(record_state.data, "", track.name, 5,
		&records, NULL) == MH_ERROR_NONE);
	mh_activity_init(&activity);
	CHECK(sizeof(RESOURCE_ROOT "/Demos/JeePen.MDE") <= sizeof(demo));
	strcpy(demo, RESOURCE_ROOT);
	strcat(demo, "/Demos/JeePen.MDE");
	CHECK(mh_horn_catalog_load_from(mh_horn_fallback_source(), &resources,
		RESOURCE_ROOT, &horns, NULL) == MH_ERROR_NONE);
	CHECK(mh_profile_catalog_load_from(mh_profile_fallback_source(),
		&resources, RESOURCE_ROOT, &horns, &random, &catalog,
		NULL) == MH_ERROR_NONE);

	CHECK(mh_settings_init(&settings, "Lifecycle", 0, 0, renderer, NULL) ==
		MH_ERROR_NONE);
	settings.game_type = 2;
	settings.av.effects_volume = settings.av.music_volume = 0;
	options.resources = NULL;
	quit = 0;
	take = NULL;
	reads = resource_state.reads;
	CHECK(mh_single_session_open(RESOURCE_ROOT, NULL, &horns, &settings,
		&options, &random, &quit, &take, &activity,
		NULL) == MH_ERROR_INVALID);
	CHECK(resource_state.reads == reads && !mh_activity_is_open(&activity));
	options.resources = &resources;
	resource_state.fail_at = reads + 40;
	observe_loading(0);
	status = mh_single_session_open(RESOURCE_ROOT, NULL, &horns, &settings,
		&options, &random, &quit, &take, &activity, &error);

	if (status != MH_ERROR_IO)
		fprintf(stderr, "Failed-load check: %d %s\n", (int)status,
			error.message == NULL ? "success" : error.message);

	CHECK(status == MH_ERROR_IO);
	CHECK(resource_state.reads == resource_state.fail_at);
	CHECK(!mh_activity_is_open(&activity) && take == NULL);
	CHECK(loading.frames >=
		(renderer == MH_RENDER_CHOICE_SOFTWARE ? 2U : 1U));
	CHECK(loading.frames < LOADING_FRAMES && loading.asset_reads > 0);
	CHECK(loading.opens == 1 && loading.closes == 1);
	CHECK(loading.renderer == NULL);
	loading.enabled = 0;
	CHECK(directory_empty(recordings));
	resource_state.fail_at = 0;

	/* Failed presentation must abort the in-flight asset load. */
	if (renderer == MH_RENDER_CHOICE_SOFTWARE) {
		observe_loading(0);
		loading.fail_frame = 4;
		CHECK(mh_single_session_open(RESOURCE_ROOT, NULL, &horns,
			&settings, &options, &random, &quit, &take,
			&activity, &error) == MH_ERROR_BACKEND);
		CHECK(strcmp(error.message,
			"cannot advance loading progress") == 0);
		CHECK(!mh_activity_is_open(&activity) && take == NULL);
		CHECK(loading.frames == 3 &&
			loading.asset_reads > loading.reads_at_frame[2]);
		CHECK(loading.opens == 1 && loading.closes == 1);
		CHECK(loading.renderer == NULL);
		loading.enabled = 0;
		CHECK(directory_empty(recordings));
	}

	CHECK(load_memory_demo(&resources, demo, &bytes) == 0);
	held = bytes;
	resource_state.fail_at = resource_state.reads + 40;
	CHECK(mh_demo_session_open_bytes(RESOURCE_ROOT, &bytes, "fixture",
		MH_DEMO_SESSION_GHOST, &horns, &settings, &options,
		&random, &quit, &take, &activity, &error) == MH_ERROR_IO);
	CHECK(resource_state.reads == resource_state.fail_at);
	CHECK(bytes.data == held.data && bytes.size == held.size);
	CHECK(!mh_activity_is_open(&activity) && take == NULL);
	free(bytes.data);
	resource_state.fail_at = 0;

	/* Cancel before the first frame and after one frame, then reopen. */
	for (mode = 0; mode < 6; mode++) {
		for (frames = 0; frames < 2; frames++) {
			CHECK(mh_settings_init(&settings, "Lifecycle", 0, 0,
				renderer, NULL) == MH_ERROR_NONE);
			settings.av.width = 640;
			settings.av.height = 400;
			settings.game_type = 2;
			settings.record_race = 1;
			settings.av.effects_volume = 0;
			settings.av.music_volume = 0;
			settings.av.triple_buffer = 1;
			take = NULL;
			quit = manual_exit = 0;
			samples = clock_state.samples;
			reads = resource_state.reads;
			record_reads = record_state.reads;
			roots = record_state.roots;

			if (mode >= 3)
				CHECK(load_memory_demo(
					&resources, demo, &bytes) == 0);

			observe_loading(mode == 0);
			loading.fail_history = mode == 0;

			if (mode == 0) {
				status = mh_single_session_open(RESOURCE_ROOT,
					NULL, &horns, &settings, &options,
					&random, &quit, &take, &activity,
					&error);
			} else if (mode == 1)
				status = mh_demo_session_open(RESOURCE_ROOT,
					demo, 1, &horns, &settings, &options,
					&random, &quit, &take, &activity,
					&error);
			else if (mode == 2)
				status = mh_attract_session_open(RESOURCE_ROOT,
					&catalog, &horns, &settings, &options,
					previous, &random, &manual_exit, &quit,
					&activity, &error);
			else {
				status = mh_demo_session_open_bytes(
					RESOURCE_ROOT, &bytes, "fixture",
					(enum mh_demo_session_mode)(mode - 3),
					&horns, &settings, &options, &random,
					&quit, &take, &activity, &error);
				CHECK(bytes.data == NULL && bytes.size == 0);
			}

			if (status != MH_ERROR_NONE)
				fprintf(stderr, "%s\n", error.message);

			CHECK(status == MH_ERROR_NONE &&
				mh_activity_is_open(&activity));
			CHECK(check_loading_frames(
				renderer, mode == 0 || mode == 2) == 0);
			CHECK(loading.opens == 2 && loading.closes == 1);
			CHECK(loading.handoff && loading.window != 0);

			if (shared_window)
				CHECK(loading.window ==
					mh_window_id(options.window));

			CHECK(loading.history_requests ==
				(renderer == MH_RENDER_CHOICE_DIRECT3D
						? (mode == 0 ? 2U : 1U)
						: 0U));
			CHECK(settings.av.triple_buffer == 1);

			if (mode == 2)
				CHECK(check_loading_records(previous, 25) == 0);
			else if (mode != 0)
				CHECK(check_loading_records("NeoCity", 10) ==
					0);

			CHECK(clock_state.samples > samples);
			CHECK(resource_state.reads > reads);
			CHECK(record_state.reads ==
				record_reads +
					(mode == 2 || mode == 3 || mode == 5
							? 2U
							: 4U));
			CHECK(record_state.roots ==
				roots +
					(mode == 2 || mode == 3 || mode == 5
							? 1U
							: 2U));
			samples = clock_state.samples;

			if (frames != 0) {
				CHECK(mh_activity_step(&activity) ==
					MH_APP_RUNNING);
				CHECK(clock_state.samples > samples);
			}

			mh_activity_close(&activity);
			mh_activity_close(&activity);

			CHECK(loading.closes == 2);
			loading.enabled = 0;

			CHECK(!mh_activity_is_open(&activity));
			CHECK(mh_activity_step(&activity) == MH_APP_DONE);
			CHECK(mh_activity_error(&activity, NULL) ==
				MH_ERROR_NONE);
			CHECK(take == NULL && quit == 0 && manual_exit == 0);
			CHECK(directory_empty(recordings));
			CHECK(record_state.writes == 0);
			count = 0;
			CHECK(mh_user_data_list(record_state.data,
				options.recording_directory,
				count_recordings, &count,
				NULL) == MH_ERROR_NONE);
			CHECK(count == 0);
		}
	}

	CHECK(check_league_assets(&resources, &settings, &options) == 0);
	CHECK(check_network_assets(&resources, &settings, &timer, &options) ==
		0);

	/* Backend diagnostics must survive race and activity teardown. */
	for (mode = 0; mode < 3; mode++) {
		CHECK(mh_settings_init(&settings, "Failure", 0, 0, renderer,
			NULL) == MH_ERROR_NONE);
		settings.av.width = 640;
		settings.av.height = 400;
		settings.av.effects_volume = settings.av.music_volume = 0;
		settings.game_type = 2;
		settings.record_race = 0;
		take = NULL;
		observe_loading(0);

		if (mode == 0)
			status = mh_single_session_open(RESOURCE_ROOT, NULL,
				&horns, &settings, &options, &random, &quit,
				&take, &activity, &error);
		else if (mode == 1)
			status = mh_demo_session_open(RESOURCE_ROOT, demo, 1,
				&horns, &settings, &options, &random, &quit,
				&take, &activity, &error);
		else
			status = mh_attract_session_open(RESOURCE_ROOT,
				&catalog, &horns, &settings, &options, previous,
				&random, &manual_exit, &quit, &activity,
				&error);

		if (status != MH_ERROR_NONE)
			fprintf(stderr, "Failure fixture %u: %s\n", mode,
				error.message);

		CHECK(status == MH_ERROR_NONE);
		loading.fail_race = 1;
		CHECK(mh_activity_step(&activity) == MH_APP_FAILED);
		CHECK(mh_activity_error(&activity, &error) == MH_ERROR_BACKEND);
		CHECK(strcmp(error.message,
			"injected race presentation failure") == 0);
		CHECK(strcmp(loading.failure_text,
			"renderer has been destroyed") == 0);
		mh_activity_close(&activity);
		CHECK(mh_activity_error(&activity, &error) == MH_ERROR_BACKEND);
		CHECK(strcmp(error.message,
			"injected race presentation failure") == 0);
		CHECK(take == NULL && directory_empty(recordings));
		loading.fail_race = loading.enabled = 0;
	}

	/*
	 * The native runner must release a session when this host cannot wait.
	 */
	CHECK(mh_single_session_run(RESOURCE_ROOT, NULL, &horns, &settings,
		&options, &random, &quit, &take,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(take == NULL && directory_empty(recordings));

	if (shared_window) {
		CHECK(mh_window_set_title(options.window,
			"Session cleanup retained window",
			NULL) == MH_ERROR_NONE);
		mh_window_destroy(options.window);
		mh_window_system_release(options.windows);
	}

	mh_memory_user_data_close(record_state.data);

	puts("Local sessions: memory recording input/output, explicit resources, failed input adoption, early/first-frame cancellation, recording abort, retained backend failures and reentry passed");

	return 0;
}

int
main(int argc, char *argv[])
{
	int result;
	unsigned int renderer;

	renderer = MH_RENDER_CHOICE_GLIDE;

	if (argc < 3 || argc > 5)
		return 2;

	if (argc >= 4 && !mh_render_choice_parse(argv[3], &renderer))
		return 2;

	if (argc == 5 && strcmp(argv[4], "--shared-window") != 0)
		return 2;

	result = check_sessions(argv[1], argv[2], renderer, argc == 5);
	mh_platform_shutdown();

	return result;
}
