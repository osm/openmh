#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/input_provider.h"
#include "../support/menu_tables.h"
#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/audio/sdl_provider.h"
#include "backends/catalog/provider.h"
#include "backends/collision/provider.h"
#include "backends/font/provider.h"
#include "backends/geometry/lob_provider.h"
#include "backends/geometry/myo_provider.h"
#include "backends/geometry/myw_provider.h"
#include "backends/image/act_provider.h"
#include "backends/image/pdi_provider.h"
#include "backends/image/png_provider.h"
#include "backends/image/provider.h"
#include "backends/image/remap_provider.h"
#include "backends/image/tga_provider.h"
#include "backends/input/sdl_provider.h"
#include "backends/material/provider.h"
#include "backends/navigation/provider.h"
#include "backends/render/sdl_canvas_provider.h"
#include "backends/retro/provider.h"
#include "backends/sprite/spr_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_user_data.h"
#include "backends/ui/dta_provider.h"
#include "backends/window/sdl_provider.h"
#include "client/application/application.h"
#include "client/application/application_internal.h"
#include "client/diagnostics/media_video.h"
#include "client/diagnostics/render_capture.h"
#include "client/diagnostics/render_renderer.h"
#include "client/diagnostics/runtime_network.h"
#include "client/hosts/network.h"
#include "client/hosts/sdl_application.h"
#include "client/hosts/sdl_video.h"
#include "client/menus/internal.h"
#include "engine/assets/tga.h"
#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/settings/config.h"
#include "engine/storage/picture_path.h"
#include "engine/storage/picture_store.h"
#include "engine/storage/storage.h"
#include "interfaces/animation/source_backend.h"
#include "interfaces/audio/backend.h"
#include "interfaces/audio/environment_source_backend.h"
#include "interfaces/audio/horn_source_backend.h"
#include "interfaces/catalog/profile_source_backend.h"
#include "interfaces/catalog/source_backend.h"
#include "interfaces/clock/backend.h"
#include "interfaces/geometry/decoder_backend.h"
#include "interfaces/image/atlas_source_backend.h"
#include "interfaces/input/backend.h"
#include "interfaces/input/clipboard_backend.h"
#include "interfaces/material/source_backend.h"
#include "interfaces/navigation/source_backend.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/render/service.h"
#include "interfaces/retro/source_backend.h"
#include "interfaces/storage/resource_backend.h"
#include "interfaces/window/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "application lifecycle line %d: %s\n", \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct clock_fixture {
	unsigned long	origin;
	unsigned int	samples, frequencies, ticks, calendar_reads;
};

enum window_failure {
	WINDOW_ACQUIRE_FAILURE,
	WINDOW_OPEN_FAILURE,
	WINDOW_INCOMPATIBLE
};

struct window_fixture {
	enum window_failure	failure;
	unsigned int		acquires, releases, opens, destroys;
	char			message[64];
};

enum host_failure {
	HOST_SUCCEEDS,
	HOST_RESOURCE_FAILURE,
	HOST_DIRECTORY_FAILURE,
	HOST_CONTROL_FAILURE
};

struct resource_fixture {
	const struct mh_user_data	*user_data;
	unsigned int	data_reads, data_writes, directories, fail_commit;
	unsigned int directory_creates, directory_opens, directory_checks,
		fail_directory;
	unsigned int ready, reads, ranges, queries, premature, fail,
		menu_sounds;
	unsigned int			resource_directories, data_browses;
	unsigned int			deny_tables, table_reads;
};

struct canvas_fixture {
	unsigned int	opened, closed, drawn, presented;
};

static struct mh_clock_counter
read_counter(void *context)
{
	struct clock_fixture *fixture;
	struct mh_clock_counter value;

	fixture = context;
	fixture->samples++;
	value.low = fixture->origin + fixture->samples * 17;
	value.high = 0;

	return value;
}

static struct mh_clock_counter
read_frequency(void *context)
{
	struct clock_fixture *fixture;
	struct mh_clock_counter value;

	fixture = context;
	fixture->frequencies++;
	value.low = 1000;
	value.high = 0;

	return value;
}

static unsigned long
ticks(void *context)
{
	struct clock_fixture *fixture;

	fixture = context;
	fixture->ticks++;

	return fixture->origin + fixture->ticks * 17;
}

static enum mh_error_code
read_calendar(
	void *context, struct mh_clock_date *output, struct mh_error *error_out)
{
	struct clock_fixture *fixture;

	fixture = context;
	fixture->calendar_reads++;
	output->year = 2030;
	output->month = 12;
	output->day = 31;
	output->hour = 12;
	output->minute = 34;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const struct mh_clock_ops clock_ops = {
	read_counter, read_frequency, ticks, NULL, read_calendar};

static enum mh_storage_status
begin_storage(void *context, enum mh_storage_operation operation,
	struct mh_error *error_out)
{
	(void)context;

	(void)operation;
	(void)error_out;

	return MH_STORAGE_PENDING;
}

static enum mh_storage_status
poll_storage(void *context, struct mh_error *error_out)
{
	(void)context;
	(void)error_out;

	return MH_STORAGE_PENDING;
}

static void
cancel_storage(void *context)
{
	unsigned int *cancellations;

	cancellations = context;
	(*cancellations)++;
}

static int
check_services(void)
{
	static const struct mh_storage_ops storage_ops = {
		begin_storage, poll_storage, cancel_storage};

	struct clock_fixture first_fixture, second_fixture;
	struct mh_clock_source first_clock, second_clock;
	struct mh_app_services services;
	struct mh_render_service rendering;
	struct mh_application *app, *other;
	struct mh_storage *storage;
	char *help[] = {"motorhead", "--help"};
	char *network_help[] = {
		"motorhead", "--connect", "localhost:16000", "--help"};

	char game_directory[] = "host-game";
	unsigned int cancellations;

	memset(&first_fixture, 0, sizeof(first_fixture));
	memset(&second_fixture, 0, sizeof(second_fixture));
	first_fixture.origin = 1000;
	second_fixture.origin = 5000;
	cancellations = 0;
	CHECK(mh_clock_source_init(&first_clock, &clock_ops, &first_fixture,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_clock_source_init(&second_clock, &clock_ops, &second_fixture,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_storage_create(&storage_ops, &cancellations, &storage, NULL) ==
		MH_ERROR_NONE);
	services.game_directory = NULL;
	services.lifecycle = NULL;
	services.image_source = mh_image_pdi_source();
	services.font_source = mh_font_fallback_source();
	services.wireframe_source = mh_wireframe_lob_source();
	services.sprite_source = mh_sprite_spr_source();
	services.model_decoder = mh_model_myo_decoder();
	services.world_decoder = mh_world_myw_decoder();
	services.atlas_source = mh_atlas_remap_source();
	services.animation_source = mh_animation_lightwave_source();
	services.navigation_source = mh_navigation_mot_dat_source();
	services.catalog_source = mh_catalog_fallback_source();
	services.profile_source = mh_profile_fallback_source();
	services.palette_source = mh_palette_act_source();
	services.layout_source = mh_menu_layout_dta_source();
	services.material_source = mh_material_mat_srf_source();
	services.env_sound_source = mh_env_sounds_esd_source();
	services.horn_source = mh_horn_fallback_source();
	services.collision_source = mh_collision_fallback_source();
	services.image_decoder = mh_image_fallback_decoder();
	services.thumbnail_decoder = mh_image_tga_thumbnail_decoder();
	services.portrait_decoder = mh_image_tga_portrait_decoder();
	services.rendering = &rendering;
	rendering.renderers = mh_diagnostic_renderers();
	rendering.canvases = mh_sdl_canvas_provider();
	rendering.windows = mh_sdl_window_system();
	services.inputs = mh_sdl_input_provider();
	services.clipboard = mh_sdl_clipboard_provider();
	services.audio = mh_sdl_audio_provider();
	services.retro_source = mh_retro_fallback_source();
	services.audio_decoder = mh_audio_fallback_decoder();
	services.audio_trace = 0;
	services.videos = mh_host_sdl_videos();
	services.network = mh_host_network();
	services.legacy_network = mh_host_legacy_network();
	services.network_trace = 0;
	rendering.capture = mh_diagnostic_capture();
	services.screenshot_directory = NULL;
	services.screenshot_encoders[MH_SCREENSHOT_PNG] =
		mh_image_png_encoder();
	services.screenshot_encoders[MH_SCREENSHOT_TGA] =
		mh_image_tga_encoder();
	services.user_data = mh_filesystem_user_data();
	services.screenshot_data = services.user_data;
	services.resources = mh_filesystem_resources();
	services.resource_archives = NULL;
	services.clock = NULL;
	services.storage = storage;
	app = NULL;
	CHECK(mh_application_open_with_services(
		2, help, &services, &app, NULL) == MH_ERROR_INVALID);
	CHECK(app == NULL && mh_storage_status(storage) == MH_STORAGE_READY);
	services.clock = &first_clock;
	services.resources = NULL;
	services.resource_archives = NULL;
	CHECK(mh_application_open_with_services(
		2, help, &services, &app, NULL) == MH_ERROR_INVALID);
	CHECK(app == NULL && first_fixture.samples == 0 &&
		mh_storage_status(storage) == MH_STORAGE_READY);
	services.resources = mh_filesystem_resources();
	services.resource_archives = NULL;
	services.user_data = NULL;
	CHECK(mh_application_open_with_services(
		2, help, &services, &app, NULL) == MH_ERROR_INVALID);
	CHECK(app == NULL && first_fixture.samples == 0 &&
		mh_storage_status(storage) == MH_STORAGE_READY);
	services.user_data = mh_filesystem_user_data();
	services.screenshot_data = services.user_data;
	services.image_source = mh_image_pdi_source();
	services.font_source = mh_font_fallback_source();
	services.wireframe_source = mh_wireframe_lob_source();
	services.sprite_source = mh_sprite_spr_source();
	services.model_decoder = mh_model_myo_decoder();
	services.world_decoder = mh_world_myw_decoder();
	services.atlas_source = mh_atlas_remap_source();
	services.animation_source = mh_animation_lightwave_source();
	services.navigation_source = mh_navigation_mot_dat_source();
	services.catalog_source = mh_catalog_fallback_source();
	services.profile_source = mh_profile_fallback_source();
	services.palette_source = mh_palette_act_source();
	services.layout_source = mh_menu_layout_dta_source();
	services.material_source = mh_material_mat_srf_source();
	services.env_sound_source = mh_env_sounds_esd_source();
	services.horn_source = mh_horn_fallback_source();
	services.collision_source = mh_collision_fallback_source();
	services.image_decoder = mh_image_fallback_decoder();
	services.thumbnail_decoder = mh_image_tga_thumbnail_decoder();
	services.portrait_decoder = mh_image_tga_portrait_decoder();
	services.rendering = NULL;
	CHECK(mh_application_open_with_services(
		2, help, &services, &app, NULL) == MH_ERROR_INVALID);
	CHECK(app == NULL && mh_storage_status(storage) == MH_STORAGE_READY);
	services.game_directory = NULL;
	services.lifecycle = NULL;
	services.image_source = mh_image_pdi_source();
	services.font_source = mh_font_fallback_source();
	services.wireframe_source = mh_wireframe_lob_source();
	services.sprite_source = mh_sprite_spr_source();
	services.model_decoder = mh_model_myo_decoder();
	services.world_decoder = mh_world_myw_decoder();
	services.atlas_source = mh_atlas_remap_source();
	services.animation_source = mh_animation_lightwave_source();
	services.navigation_source = mh_navigation_mot_dat_source();
	services.catalog_source = mh_catalog_fallback_source();
	services.profile_source = mh_profile_fallback_source();
	services.palette_source = mh_palette_act_source();
	services.layout_source = mh_menu_layout_dta_source();
	services.material_source = mh_material_mat_srf_source();
	services.env_sound_source = mh_env_sounds_esd_source();
	services.horn_source = mh_horn_fallback_source();
	services.collision_source = mh_collision_fallback_source();
	services.image_decoder = mh_image_fallback_decoder();
	services.thumbnail_decoder = mh_image_tga_thumbnail_decoder();
	services.portrait_decoder = mh_image_tga_portrait_decoder();
	services.rendering = &rendering;
	rendering.windows = NULL;
	CHECK(mh_application_open_with_services(
		2, help, &services, &app, NULL) == MH_ERROR_INVALID);
	CHECK(app == NULL && first_fixture.samples == 0 &&
		mh_storage_status(storage) == MH_STORAGE_READY);
	rendering.windows = mh_sdl_window_system();
	services.inputs = mh_sdl_input_provider();
	services.clipboard = mh_sdl_clipboard_provider();
	services.audio = mh_sdl_audio_provider();
	services.retro_source = mh_retro_fallback_source();
	services.audio_decoder = mh_audio_fallback_decoder();
	services.audio_trace = 0;
	services.videos = mh_host_sdl_videos();
	services.network = mh_host_network();
	services.legacy_network = mh_host_legacy_network();
	services.network_trace = 0;
	rendering.canvases = NULL;
	CHECK(mh_application_open_with_services(
		2, help, &services, &app, NULL) == MH_ERROR_INVALID);
	CHECK(app == NULL && first_fixture.samples == 0 &&
		mh_storage_status(storage) == MH_STORAGE_READY);
	rendering.canvases = mh_sdl_canvas_provider();
	rendering.renderers = NULL;
	CHECK(mh_application_open_with_services(
		2, help, &services, &app, NULL) == MH_ERROR_INVALID);
	CHECK(app == NULL && first_fixture.samples == 0 &&
		mh_storage_status(storage) == MH_STORAGE_READY);
	services.game_directory = NULL;
	services.lifecycle = NULL;
	services.image_source = mh_image_pdi_source();
	services.font_source = mh_font_fallback_source();
	services.wireframe_source = mh_wireframe_lob_source();
	services.sprite_source = mh_sprite_spr_source();
	services.model_decoder = mh_model_myo_decoder();
	services.world_decoder = mh_world_myw_decoder();
	services.atlas_source = mh_atlas_remap_source();
	services.animation_source = mh_animation_lightwave_source();
	services.navigation_source = mh_navigation_mot_dat_source();
	services.catalog_source = mh_catalog_fallback_source();
	services.profile_source = mh_profile_fallback_source();
	services.palette_source = mh_palette_act_source();
	services.layout_source = mh_menu_layout_dta_source();
	services.material_source = mh_material_mat_srf_source();
	services.env_sound_source = mh_env_sounds_esd_source();
	services.horn_source = mh_horn_fallback_source();
	services.collision_source = mh_collision_fallback_source();
	services.image_decoder = mh_image_fallback_decoder();
	services.thumbnail_decoder = mh_image_tga_thumbnail_decoder();
	services.portrait_decoder = mh_image_tga_portrait_decoder();
	services.rendering = &rendering;
	rendering.renderers = mh_diagnostic_renderers();
	services.inputs = NULL;
	CHECK(mh_application_open_with_services(
		2, help, &services, &app, NULL) == MH_ERROR_INVALID);
	CHECK(app == NULL && first_fixture.samples == 0 &&
		mh_storage_status(storage) == MH_STORAGE_READY);
	services.inputs = mh_sdl_input_provider();
	services.clipboard = mh_sdl_clipboard_provider();
	services.audio = mh_sdl_audio_provider();
	services.retro_source = mh_retro_fallback_source();
	services.audio_decoder = mh_audio_fallback_decoder();
	services.audio_trace = 0;
	services.videos = mh_host_sdl_videos();
	services.network = mh_host_network();
	services.legacy_network = mh_host_legacy_network();
	services.network_trace = 0;
	CHECK(mh_storage_begin(storage, MH_STORAGE_PREPARE, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_application_open_with_services(
		2, help, &services, &app, NULL) == MH_ERROR_STATE);
	CHECK(app == NULL && cancellations == 0 && first_fixture.samples == 0);
	mh_storage_close(storage);
	CHECK(cancellations == 1);

	CHECK(mh_storage_open(&services.storage, NULL) == MH_ERROR_NONE);
	services.game_directory = game_directory;
	CHECK(mh_application_open_with_services(
		2, help, &services, &app, NULL) == MH_ERROR_NONE);
	game_directory[0] = 'X';
	CHECK(strcmp(app->asset_root, "host-game") == 0);
	CHECK(first_fixture.samples == 2 && first_fixture.frequencies == 2 &&
		second_fixture.samples == 0);
	CHECK(app->session_options.clipboard == mh_sdl_clipboard_provider());
	CHECK(mh_storage_open(&storage, NULL) == MH_ERROR_NONE);
	services.storage = storage;
	services.clock = &second_clock;
	other = app;
	CHECK(mh_application_open_with_services(
		2, help, &services, &other, NULL) == MH_ERROR_STATE);
	CHECK(other == app && second_fixture.samples == 0 &&
		mh_storage_status(storage) == MH_STORAGE_READY);
	CHECK(mh_application_step(app) == MH_APP_DONE);
	CHECK(mh_application_close(app) == 0);

	CHECK(mh_application_open_with_services(
		4, network_help, &services, &app, NULL) == MH_ERROR_NONE);
	CHECK(app->session_options.network_wire == MH_NET_WIRE_31_LOBBY);
	CHECK(app->session_options.network ==
		(app->session_options.network_legacy ? services.legacy_network
						: services.network));
	rendering.renderers = NULL;
	rendering.canvases = NULL;
	rendering.windows = NULL;
	services.inputs = NULL;
	services.clipboard = NULL;
	services.audio = NULL;
	services.clock = NULL;
	services.storage = NULL;
	services.user_data = NULL;
	services.resources = NULL;
	services.resource_archives = NULL;
	CHECK(mh_application_step(app) == MH_APP_DONE);
	CHECK(mh_application_close(app) == 0);
	CHECK(first_fixture.samples == 2 && first_fixture.frequencies == 2);
	CHECK(second_fixture.samples == 2 && second_fixture.frequencies == 2);

	return 0;
}

static int
check_network_arguments(void)
{
	struct mh_application *app;
	char *defaults[] = {"motorhead"};
	char *laninet[] = {"motorhead", "--network", "laninet"};
	char *port[] = {"motorhead", "--connect", "localhost:23456"};
	char *invalid_port[] = {"motorhead", "--connect", "localhost:0"};

	char *unknown[] = {"motorhead", "--network", "unknown"};

	app = calloc(1, sizeof(*app));
	CHECK(app != NULL);
	CHECK(mh_application_parse_arguments(app, 1, defaults) == -1);
	CHECK(app->session_options.network_wire == MH_NET_WIRE_31_LOBBY);
	CHECK(mh_application_parse_arguments(app, 3, port) == -1 &&
		strcmp(app->connect_address, "localhost:23456") == 0);
	CHECK(app->session_options.network_wire == MH_NET_WIRE_31_LOBBY);
	CHECK(mh_application_parse_arguments(app, 3, invalid_port) > 0);
	CHECK(mh_application_parse_arguments(app, 3, laninet) > 0);
	CHECK(mh_application_parse_arguments(app, 3, unknown) > 0);
	free(app);

	return 0;
}

static int
check_arguments(void)
{
	struct mh_application *application, *other;
	char *help[] = {"motorhead", "--help"};
	char *invalid[] = {"motorhead", "--debug-car", "0"};
	unsigned int i;

	application = NULL;
	CHECK(mh_application_open(0, NULL, &application, NULL) !=
		MH_ERROR_NONE);
	CHECK(application == NULL);

	for (i = 0; i < 2; i++) {
		CHECK(mh_application_open(2, help, &application, NULL) ==
			MH_ERROR_NONE);
		other = application;
		CHECK(mh_application_open(2, help, &other, NULL) !=
			MH_ERROR_NONE);
		CHECK(other == application);
		CHECK(mh_application_step(application) == MH_APP_DONE);
		CHECK(mh_application_step(application) == MH_APP_DONE);
		CHECK(mh_application_delay(application) == 0);
		CHECK(mh_application_close(application) == 0);
		CHECK(mh_application_open(3, invalid, &application, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_application_step(application) == MH_APP_FAILED);
		CHECK(mh_application_step(application) == MH_APP_FAILED);
		CHECK(mh_application_close(application) != 0);
	}

	return 0;
}

static int
capture_exists(const char *path)
{
	FILE *file;

	file = fopen(path, "rb");

	if (file == NULL)
		return 0;

	fclose(file);

	return 1;
}

static enum mh_error_code
acquire_windows(void *context, const struct mh_window_system_options *options,
	struct mh_error *error_out)
{
	struct window_fixture *fixture;

	fixture = context;
	fixture->acquires++;

	if (fixture->failure == WINDOW_ACQUIRE_FAILURE)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, fixture->message);

	return mh_window_system_acquire(
		mh_sdl_window_system(), options, error_out);
}

static void
release_windows(void *context)
{
	struct window_fixture *fixture;

	fixture = context;
	fixture->releases++;
	strcpy(fixture->message, "video released");
	mh_window_system_release(mh_sdl_window_system());
}

static enum mh_error_code
open_window(void *context, const char *title, unsigned int width,
	unsigned int height, unsigned int flags, void **output,
	struct mh_error *error_out)
{
	const struct mh_window_system *native;
	struct window_fixture *fixture;

	fixture = context;
	fixture->opens++;

	if (fixture->failure == WINDOW_OPEN_FAILURE)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, fixture->message);

	native = mh_sdl_window_system();

	return native->ops->open(native->context, title, width, height, flags,
		output, error_out);
}

static void
destroy_window(void *context, void *window)
{
	const struct mh_window_system *native;
	struct window_fixture *fixture;

	fixture = context;
	fixture->destroys++;
	native = mh_sdl_window_system();
	native->ops->destroy(native->context, window);
}

static int
window_failure(char *assets, enum window_failure failure)
{
	struct mh_window_ops ops;
	struct mh_window_system windows;
	struct window_fixture fixture;
	struct clock_fixture ticks;
	struct mh_clock_source clock;
	struct mh_app_services services;
	struct mh_render_service rendering;
	struct mh_application *app;
	enum mh_app_status status;
	unsigned int steps;
	char *args[] = {"motorhead", "--debug-no-intro", "--debug-no-settings",
		"--debug-no-records", "--audio", "0", "--assets", NULL,
		"--debug-capture", "unused-window-failure.bmp"};

	memset(&fixture, 0, sizeof(fixture));
	memset(&ticks, 0, sizeof(ticks));
	memset(&services, 0, sizeof(services));
	memset(&rendering, 0, sizeof(rendering));
	fixture.failure = failure;
	strcpy(fixture.message, "selected window provider failed");
	ops = *mh_sdl_window_system()->ops;
	ops.acquire = acquire_windows;
	ops.release = release_windows;
	ops.open = open_window;
	ops.destroy = destroy_window;
	ops.modes = NULL;
	CHECK(mh_window_system_init(&windows, &ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_clock_source_init(&clock, &clock_ops, &ticks, NULL) ==
		MH_ERROR_NONE);
	services.game_directory = NULL;
	services.lifecycle = NULL;
	services.image_source = mh_image_pdi_source();
	services.font_source = mh_font_fallback_source();
	services.wireframe_source = mh_wireframe_lob_source();
	services.sprite_source = mh_sprite_spr_source();
	services.model_decoder = mh_model_myo_decoder();
	services.world_decoder = mh_world_myw_decoder();
	services.atlas_source = mh_atlas_remap_source();
	services.animation_source = mh_animation_lightwave_source();
	services.navigation_source = mh_navigation_mot_dat_source();
	services.catalog_source = mh_catalog_fallback_source();
	services.profile_source = mh_profile_fallback_source();
	services.palette_source = mh_palette_act_source();
	services.layout_source = mh_menu_layout_dta_source();
	services.material_source = mh_material_mat_srf_source();
	services.env_sound_source = mh_env_sounds_esd_source();
	services.horn_source = mh_horn_fallback_source();
	services.collision_source = mh_collision_fallback_source();
	services.image_decoder = mh_image_fallback_decoder();
	services.thumbnail_decoder = mh_image_tga_thumbnail_decoder();
	services.portrait_decoder = mh_image_tga_portrait_decoder();
	services.rendering = &rendering;
	rendering.renderers = mh_diagnostic_renderers();
	rendering.canvases = mh_sdl_canvas_provider();
	rendering.windows = &windows;
	services.inputs = mh_sdl_input_provider();
	services.clipboard = NULL;
	services.audio = mh_sdl_audio_provider();
	services.retro_source = mh_retro_fallback_source();
	services.audio_decoder = mh_audio_fallback_decoder();
	services.audio_trace = 0;
	services.videos = mh_host_sdl_videos();
	services.network = mh_host_network();
	services.legacy_network = mh_host_legacy_network();
	services.network_trace = 0;
	services.clock = &clock;
	services.resources = mh_filesystem_resources();
	services.resource_archives = NULL;
	CHECK(mh_memory_user_data_open(&services.user_data, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_storage_open(&services.storage, NULL) == MH_ERROR_NONE);
	args[7] = assets;
	CHECK(mh_application_open_with_services(
		(int)(sizeof(args) / sizeof(args[0])), args, &services,
		&app, NULL) == MH_ERROR_NONE);
	steps = 0;

	do {
		status = mh_application_step(app);
		steps++;
		CHECK(steps < 20);
	} while (status == MH_APP_RUNNING);

	CHECK(status == MH_APP_FAILED && fixture.acquires == 1 &&
		fixture.releases == 0 && fixture.destroys == 0);
	CHECK(fixture.opens == (failure == WINDOW_ACQUIRE_FAILURE ? 0 : 1));
	CHECK(app->menus != NULL &&
		app->menus->error.code ==
			(failure == WINDOW_INCOMPATIBLE ? MH_ERROR_UNSUPPORTED
							: MH_ERROR_BACKEND));

	if (failure != WINDOW_INCOMPATIBLE) {
		/*
		 * Retained diagnostics must survive another provider operation.
		 */
		strcpy(fixture.message, "provider diagnostic replaced");
		CHECK(strcmp(app->menus->error.message,
			"selected window provider failed") == 0);
	} else
		CHECK(strstr(app->menus->error.message, "incompatible") !=
			NULL);

	CHECK(mh_application_close(app) == 1);
	CHECK(fixture.releases == (failure == WINDOW_ACQUIRE_FAILURE ? 0 : 1));
	CHECK(fixture.destroys == (failure == WINDOW_INCOMPATIBLE ? 1 : 0));
	mh_memory_user_data_close(services.user_data);

	return 0;
}

static int
check_frames(char *assets, char *capture)
{
	struct mh_application *application;
	enum mh_app_status status;
	unsigned int cancel, steps;
	char *args[] = {"motorhead", "--debug-no-intro", "--debug-no-settings",
		"--debug-no-records", "--audio", "0", "--assets", NULL,
		"--debug-capture", NULL, "--debug-frames", "2"};

	args[7] = assets;
	args[9] = capture;
	CHECK(!capture_exists(capture));

	/*
	 * Reopen after cancellation during startup, initialization and drawing.
	 */
	for (cancel = 0; cancel <= 3; cancel++) {
		CHECK(mh_application_open((int)(sizeof(args) / sizeof(args[0])),
			args, &application, NULL) == MH_ERROR_NONE);

		for (steps = 0; steps < cancel; steps++)
			CHECK(mh_application_step(application) ==
				MH_APP_RUNNING);

		CHECK(application->audio_provider == NULL &&
			application->session_options.audio_provider == NULL);
		CHECK(!capture_exists(capture));
		CHECK(mh_application_close(application) != 0);
		CHECK(!capture_exists(capture));
	}

	CHECK(mh_application_open((int)(sizeof(args) / sizeof(args[0])), args,
		&application, NULL) == MH_ERROR_NONE);
	steps = 0;

	do {
		status = mh_application_step(application);
		CHECK(mh_application_delay(application) == 0);
		CHECK(++steps < 20);
	} while (status == MH_APP_RUNNING);

	CHECK(status == MH_APP_DONE && capture_exists(capture));
	CHECK(mh_application_step(application) == MH_APP_DONE);
	CHECK(mh_application_close(application) == 0);
	CHECK(remove(capture) == 0);

	args[7] = "missing-application-lifecycle-assets";
	CHECK(mh_application_open((int)(sizeof(args) / sizeof(args[0])), args,
		&application, NULL) == MH_ERROR_NONE);
	steps = 0;

	do {
		status = mh_application_step(application);
		CHECK(++steps < 20);
	} while (status == MH_APP_RUNNING);

	CHECK(status == MH_APP_FAILED && !capture_exists(capture));
	CHECK(mh_application_step(application) == MH_APP_FAILED);
	CHECK(mh_application_close(application) != 0);

	return 0;
}

static enum mh_storage_status
prepare_resources(void *context, enum mh_storage_operation operation,
	struct mh_error *error_out)
{
	struct resource_fixture *fixture;

	fixture = context;
	(void)mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (operation == MH_STORAGE_COMMIT) {
		if (fixture->fail_commit && fixture->data_writes != 0) {
			fixture->fail_commit = 0;
			mh_error_set(error_out, MH_ERROR_IO,
				"injected settings commit failure");
			return MH_STORAGE_FAILED;
		}

		return MH_STORAGE_READY;
	}

	fixture->ready = 0;

	return MH_STORAGE_PENDING;
}

static enum mh_storage_status
resources_ready(void *context, struct mh_error *error_out)
{
	struct resource_fixture *fixture;

	fixture = context;
	fixture->ready = 1;
	(void)mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return MH_STORAGE_READY;
}

static void
cancel_resources(void *context)
{
	(void)context;
}

static enum mh_error_code
resource_access(struct resource_fixture *fixture, struct mh_error *error_out)
{
	fixture->queries++;

	if (!fixture->ready) {
		fixture->premature++;
		return mh_error_set(error_out, MH_ERROR_STATE,
			"resource queried before prepare");
	}

	return MH_ERROR_NONE;
}

static enum mh_error_code
menu_table_access(struct resource_fixture *fixture, const char *path,
	struct mh_error *error_out)
{
	const char *name;

	name = strrchr(path, '/');
	name = name == NULL ? path : name + 1;

	if (fixture->deny_tables &&
		(mh_ascii_equal(name, "sprpos.dta") ||
			mh_ascii_equal(name, "default.act"))) {
		fixture->table_reads++;
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"original menu tables unavailable");
	}

	return MH_ERROR_NONE;
}

static enum mh_error_code
read_resource(void *context, const char *path, size_t limit,
	struct mh_file *file, struct mh_error *error_out)
{
	struct resource_fixture *fixture;
	const char *name;
	enum mh_error_code status;

	fixture = context;
	status = resource_access(fixture, error_out);

	if (status == MH_ERROR_NONE)
		status = menu_table_access(context, path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture->reads++;
	name = strrchr(path, '/');

	if (name != NULL && mh_ascii_equal(name + 1, "menusound.cfg"))
		fixture->menu_sounds++;

	if (fixture->fail)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"injected menu resource failure");

	return mh_resource_source_read_bounded(
		mh_filesystem_resources(), path, limit, file, error_out);
}

static enum mh_error_code
resource_size(void *context, const char *path, unsigned long *size,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = resource_access(context, error_out);

	if (status == MH_ERROR_NONE)
		status = menu_table_access(context, path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_resource_source_size(
		mh_filesystem_resources(), path, size, error_out);
}

static enum mh_error_code
list_resources(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = resource_access(context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_resource_source_list(
		mh_filesystem_resources(), path, visit, visitor, error_out);
}

static enum mh_error_code
open_resource_directory(void *context, const char *path,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	struct resource_fixture *fixture;
	enum mh_error_code status;

	fixture = context;
	status = resource_access(fixture, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture->resource_directories++;

	return mh_resource_source_open_directory(
		mh_filesystem_resources(), path, output, error_out);
}

static enum mh_error_code
browse_user_directory(void *context, const char *path, const char *root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	struct resource_fixture *fixture;
	enum mh_error_code status;

	fixture = context;
	status = resource_access(fixture, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture->data_browses++;

	return mh_user_data_browse_directory(
		fixture->user_data, path, root, output, error_out);
}

static enum mh_error_code
read_range(void *context, const char *path, unsigned long offset, void *output,
	size_t size, struct mh_error *error_out)
{
	struct resource_fixture *fixture;
	enum mh_error_code status;

	fixture = context;
	status = resource_access(fixture, error_out);

	if (status == MH_ERROR_NONE)
		status = menu_table_access(context, path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture->ranges++;

	return mh_resource_source_read_at(mh_filesystem_resources(), path,
		offset, output, size, error_out);
}

static enum mh_error_code
read_user_data(void *context, const char *key, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	struct resource_fixture *fixture;
	enum mh_error_code status;

	fixture = context;
	status = resource_access(fixture, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture->data_reads++;

	return mh_user_data_read(
		fixture->user_data, key, limit, output, error_out);
}

static enum mh_error_code
replace_user_data(void *context, const char *key, const void *bytes,
	size_t size, struct mh_error *error_out)
{
	struct resource_fixture *fixture;
	enum mh_error_code status;

	fixture = context;
	status = resource_access(fixture, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture->data_writes++;

	return mh_user_data_replace(
		fixture->user_data, key, bytes, size, error_out);
}

static enum mh_error_code
user_directory(void *context, char **output, struct mh_error *error_out)
{
	struct resource_fixture *fixture;
	enum mh_error_code status;

	fixture = context;
	status = resource_access(fixture, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture->directories++;

	return mh_user_data_directory(fixture->user_data, output, error_out);
}

static enum mh_error_code
list_user_directory(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	struct resource_fixture *fixture;
	enum mh_error_code status;

	fixture = context;
	status = resource_access(fixture, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_user_data_list(
		fixture->user_data, path, visit, visitor, error_out);
}

static enum mh_error_code
create_user_directory(
	void *context, const char *path, struct mh_error *error_out)
{
	struct resource_fixture *fixture;
	enum mh_error_code status;

	fixture = context;
	status = resource_access(fixture, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture->directory_creates++;

	return mh_user_data_create_directory(
		fixture->user_data, path, error_out);
}

static enum mh_error_code
open_user_directory(void *context, const char *path, const char *resource_root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	struct resource_fixture *fixture;
	enum mh_error_code status;

	fixture = context;
	status = resource_access(fixture, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture->directory_opens++;

	if ((fixture->fail_directory == HOST_DIRECTORY_FAILURE &&
		strcmp(path, "Saves") == 0 &&
		fixture->directory_opens == 2) ||
		(fixture->fail_directory == HOST_CONTROL_FAILURE &&
			strcmp(path, "Saves") == 0 &&
			fixture->directory_opens == 1))
		return mh_error_set(error_out, MH_ERROR_IO,
			"injected user directory failure");

	return mh_user_data_open_directory(
		fixture->user_data, path, resource_root, output, error_out);
}

static enum mh_error_code
check_user_directory(
	void *context, const char *path, struct mh_error *error_out)
{
	struct resource_fixture *fixture;
	enum mh_error_code status;

	fixture = context;
	status = resource_access(fixture, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture->directory_checks++;

	return mh_user_data_check_directory(
		fixture->user_data, path, error_out);
}

static int
check_memory_rankings(struct mh_menus *menus, const struct mh_user_data *data)
{
	struct mh_highscore_query query;
	struct mh_highscore_table table;

	mh_menus_rankings_init(NULL, menus);

	if (menus->error.code != MH_ERROR_NONE)
		fprintf(stderr, "Memory Rankings: %s\n", menus->error.message);

	CHECK(menus->error.code == MH_ERROR_NONE && menus->rankings_loaded);
	CHECK(mh_menu_rankings_query(&menus->rankings, menus->track_titles, 8,
		&query, NULL) == MH_ERROR_NONE);
	table = menus->rankings_table;
	strcpy((char *)table.rows[0].name, "Memory scorer");
	table.dirty = 1;
	CHECK(mh_highscore_file_save_to(data, "", &query, &table, NULL) ==
		MH_ERROR_NONE);
	mh_menus_rankings_init(NULL, menus);
	CHECK(menus->error.code == MH_ERROR_NONE && menus->rankings_loaded);
	CHECK(strcmp((const char *)menus->rankings_table.rows[0].name,
		"Memory scorer") == 0);

	return 0;
}

static int
check_memory_leagues(struct mh_menus *menus, const struct mh_user_data *data)
{
	const struct mh_league_catalog *catalog;
	struct mh_league_document loaded;
	char key[256];
	unsigned long seed;

	CHECK(mh_menus_league_prepare(menus, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(menus->league_path, "Saves/") == 0);
	catalog = mh_leagues_catalog(menus->leagues);
	CHECK(catalog != NULL && catalog->count == 1 &&
		catalog->entries[0].document.created == 20301231UL);
	strcpy(key, "Saves/");
	strcat(key, catalog->entries[0].filename);
	CHECK(mh_leagues_save(menus->leagues, NULL) == MH_ERROR_NONE);
	CHECK(mh_league_file_load_from(data, key, menus->profiles->profiles,
		menus->profiles->count, &loaded, NULL) == MH_ERROR_NONE);
	CHECK(loaded.created == 20301231UL);
	seed = menus->random_state;
	CHECK(mh_menus_league_reload(menus, NULL) == MH_ERROR_NONE);
	CHECK(seed == menus->random_state);
	catalog = mh_leagues_catalog(menus->leagues);
	CHECK(catalog != NULL && catalog->count == 1 &&
		catalog->entries[0].document.created == loaded.created);

	return 0;
}

static int
save_memory_picture(struct mh_application *app)
{
	static const unsigned char pixels[] = {0, 0x7c};
	struct mh_data_directory *directory;
	struct mh_picture_store *store;
	struct mh_picture image;
	struct mh_file bytes;
	char path[MH_PICTURE_DIRECTORY_SIZE];

	memset(&image, 0, sizeof(image));
	image.format = MH_PICTURE_RGB555;
	image.width = image.height = 1;
	image.pixels = pixels;
	image.pixel_size = sizeof(pixels);
	CHECK(mh_image_tga_encode(&image, &bytes, NULL) == MH_ERROR_NONE);
	CHECK(mh_picture_directory_open_from(
		app->session_options.screenshot_data,
		app->session_options.screenshot_directory,
		app->asset_root, path, &directory,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "Saves") == 0);
	CHECK(mh_picture_store_open_in(directory, &store, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_picture_store_save(store, "motor0.tga", &bytes, NULL) ==
		MH_ERROR_NONE);
	mh_picture_store_close(store);
	free(bytes.data);

	return 0;
}

static int
check_picture(const struct mh_user_data *data)
{
	struct mh_file bytes;

	CHECK(mh_user_data_read(data, "Saves/motor0.tga", 1024, &bytes, NULL) ==
		MH_ERROR_NONE);
	CHECK(bytes.size == MH_TGA_HEADER_SIZE + 2 &&
		bytes.data[MH_TGA_HEADER_SIZE] == 0 &&
		bytes.data[MH_TGA_HEADER_SIZE + 1] == 0x7c);
	free(bytes.data);
	CHECK(mh_user_data_read(data, "Saves/motor1.tga", 1024, &bytes, NULL) ==
		MH_ERROR_NOT_FOUND);

	return 0;
}

static int
check_memory_recordings(struct mh_menus *menus)
{
	struct mh_file bytes;
	unsigned int i;

	for (i = 0; i < menus->recordings.menu.count; i++)
		if (strcmp(menus->recordings.menu.names[i], "Host") == 0)
			break;

	CHECK(i < menus->recordings.menu.count);
	CHECK(mh_recording_catalog_removable(&menus->recordings, i));
	CHECK(mh_recording_catalog_load(&menus->recordings, i, &bytes, NULL) ==
		MH_ERROR_NONE);
	CHECK(bytes.size == 14 &&
		memcmp(bytes.data, "host recording", 14) == 0);
	free(bytes.data);

	return 0;
}

static enum mh_error_code
open_canvas(void *context, struct mh_window *window,
	const struct mh_canvas_layout *layout, void **output,
	struct mh_error *error_out)
{
	struct canvas_fixture *fixture;
	struct mh_canvas *canvas;
	enum mh_error_code status;

	fixture = context;
	status = mh_canvas_open_layout(
		mh_sdl_canvas_provider(), window, layout, &canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture->opened++;
	*output = canvas;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_canvas(void *context, void *canvas)
{
	struct canvas_fixture *fixture;

	fixture = context;
	fixture->closed++;
	mh_canvas_destroy(canvas);
}

static enum mh_error_code
draw_canvas(void *context, void *canvas, const unsigned char *pixels,
	struct mh_error *error_out)
{
	struct canvas_fixture *fixture;

	fixture = context;
	fixture->drawn++;

	return mh_canvas_draw(canvas, pixels, error_out);
}

static enum mh_error_code
present_canvas(void *context, void *canvas, struct mh_error *error_out)
{
	struct canvas_fixture *fixture;

	fixture = context;
	fixture->presented++;

	return mh_canvas_present(canvas, error_out);
}

static int
check_host_frames(char *assets, enum host_failure fail)
{
	static const struct mh_canvas_ops canvas_ops = {open_canvas,
		close_canvas, draw_canvas, present_canvas, NULL, NULL};

	struct mh_test_input input;
	struct mh_test_menu_tables *tables;
	struct mh_menu_layout_source layouts;
	struct mh_palette_source palettes;
	struct mh_retro_source retro;
	unsigned int color;
	struct canvas_fixture canvas_state;
	struct mh_canvas_provider canvases;
	static const struct mh_resource_ops resource_ops = {read_resource,
		resource_size, list_resources, read_range,
		open_resource_directory};

	static const struct mh_storage_ops storage_ops = {
		prepare_resources, resources_ready, cancel_resources};

	static const struct mh_user_data_ops data_ops = {read_user_data,
		replace_user_data, user_directory, list_user_directory,
		create_user_directory, open_user_directory,
		check_user_directory, browse_user_directory};

	const struct mh_window_system *windows;
	struct mh_window *host_window;
	struct mh_user_data data;
	struct mh_file saved, after;
	const struct mh_address_book *book;
	struct resource_fixture resource_state;
	struct mh_resource_source resources;
	struct clock_fixture fixture;
	struct mh_clock_source clock;
	struct mh_app_services services;
	struct mh_model_decoder models;
	struct mh_world_decoder worlds;
	struct mh_atlas_source atlases;
	struct mh_animation_source animations;
	struct mh_navigation_source navigations;
	struct mh_catalog_source catalogs;
	struct mh_profile_source profiles;
	struct mh_material_source materials;
	struct mh_env_sound_source environment;
	struct mh_horn_source horns;
	struct mh_render_service rendering;
	struct mh_application *app;
	enum mh_app_status status;
	unsigned int steps, samples, tick_reads, retried, retry_samples, edited;
	char *args[] = {"motorhead", "--debug-no-intro", "--debug-records", "",
		"--debug-require-audio", "--assets", NULL, "--debug-frames",
		"2", "--user-data", "Saves", "--grab-mouse", "0"};

	tables = calloc(1, sizeof(*tables));
	CHECK(tables != NULL);
	CHECK(mh_menu_layout_load_from(mh_menu_layout_dta_source(),
		mh_filesystem_resources(), assets, "Data/sprpos.dta",
		&tables->layout, NULL) == MH_ERROR_NONE);
	CHECK(mh_palette_load_from(mh_palette_act_source(),
		mh_filesystem_resources(), assets, "Data/Default.ACT",
		tables->colors, NULL) == MH_ERROR_NONE);
	mh_test_menu_tables_init(tables, &layouts, &palettes);
	CHECK(mh_test_input_init(&input, NULL) == MH_ERROR_NONE);
	memset(&canvas_state, 0, sizeof(canvas_state));
	CHECK(mh_canvas_provider_init(&canvases, &canvas_ops, &canvas_state,
		NULL) == MH_ERROR_NONE);
	windows = mh_sdl_window_system();
	CHECK(mh_window_system_acquire(windows, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_window_open(windows, "Host window", 64, 64, MH_WINDOW_HIDDEN,
		&host_window, NULL) == MH_ERROR_NONE);
	memset(&fixture, 0, sizeof(fixture));
	memset(&resource_state, 0, sizeof(resource_state));
	resource_state.deny_tables = 1;
	resource_state.fail = fail == HOST_RESOURCE_FAILURE;
	resource_state.fail_directory = fail;
	resource_state.fail_commit = 1;
	CHECK(mh_memory_user_data_open(&resource_state.user_data, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_replace(resource_state.user_data, "Saves/Host.MDE",
		"host recording", 14, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_replace(resource_state.user_data, "Saves/memory.npc",
		"Type TCP\nAddress memory.example\n",
		strlen("Type TCP\nAddress memory.example\n"),
		NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_replace(resource_state.user_data,
		"Saves/keyboard.clo", "LayoutName Keyboard\nTurnLeft T\n",
		strlen("LayoutName Keyboard\nTurnLeft T\n"),
		NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_init(&data, &data_ops, &resource_state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_resource_source_init(&resources, &resource_ops,
		&resource_state, NULL) == MH_ERROR_NONE);
	fixture.origin = 1000;
	args[6] = assets;
	CHECK(mh_clock_source_init(&clock, &clock_ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	services.game_directory = NULL;
	services.lifecycle = NULL;
	services.image_source = mh_image_pdi_source();
	services.font_source = mh_font_fallback_source();
	services.wireframe_source = mh_wireframe_lob_source();
	services.sprite_source = mh_sprite_spr_source();
	models = *mh_model_myo_decoder();
	worlds = *mh_world_myw_decoder();
	atlases = *mh_atlas_remap_source();
	animations = *mh_animation_lightwave_source();
	navigations = *mh_navigation_mot_dat_source();
	catalogs = *mh_catalog_fallback_source();
	profiles = *mh_profile_fallback_source();
	materials = *mh_material_mat_srf_source();
	environment = *mh_env_sounds_esd_source();
	horns = *mh_horn_fallback_source();
	services.model_decoder = &models;
	services.world_decoder = &worlds;
	services.atlas_source = &atlases;
	services.animation_source = &animations;
	services.navigation_source = &navigations;
	services.catalog_source = &catalogs;
	services.profile_source = &profiles;
	services.palette_source = &palettes;
	services.layout_source = &layouts;
	services.material_source = &materials;
	services.env_sound_source = &environment;
	services.horn_source = &horns;
	services.collision_source = mh_collision_fallback_source();
	services.image_decoder = mh_image_fallback_decoder();
	services.thumbnail_decoder = mh_image_tga_thumbnail_decoder();
	services.portrait_decoder = mh_image_tga_portrait_decoder();
	services.rendering = &rendering;
	rendering.renderers = mh_diagnostic_renderers();
	rendering.canvases = &canvases;
	rendering.windows = mh_sdl_window_system();
	services.inputs = &input.provider;
	services.clipboard = NULL;
	services.audio = mh_sdl_audio_provider();
	retro = *mh_retro_fallback_source();
	services.retro_source = &retro;
	services.audio_decoder = mh_audio_fallback_decoder();
	services.audio_trace = 0;
	services.videos = mh_host_sdl_videos();
	services.network = mh_host_network();
	services.legacy_network = mh_host_legacy_network();
	services.network_trace = 0;
	rendering.capture = mh_diagnostic_capture();
	services.screenshot_directory = NULL;
	services.screenshot_encoders[MH_SCREENSHOT_PNG] =
		mh_image_png_encoder();
	services.screenshot_encoders[MH_SCREENSHOT_TGA] =
		mh_image_tga_encoder();
	services.user_data = &data;
	services.screenshot_data = services.user_data;
	services.resources = &resources;
	services.resource_archives = NULL;
	services.clock = &clock;
	CHECK(mh_storage_create(&storage_ops, &resource_state,
		&services.storage, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_open_with_services(
		13, args, &services, &app, NULL) == MH_ERROR_NONE);
	CHECK(fixture.samples == 2 && resource_state.queries == 0);
	CHECK(app->audio_decoder == services.audio_decoder);
	CHECK(app->audio_provider == services.audio &&
		app->session_options.audio_provider == services.audio);
	CHECK(app->session_options.image_source == services.image_source);
	CHECK(app->session_options.font_source == services.font_source);
	CHECK(app->session_options.wireframe_source ==
		services.wireframe_source);
	CHECK(app->session_options.sprite_source == services.sprite_source);
	CHECK(app->session_options.model_decoder == services.model_decoder);
	CHECK(app->session_options.world_decoder == services.world_decoder);
	CHECK(app->session_options.atlas_source == services.atlas_source);
	CHECK(app->session_options.animation_source ==
		services.animation_source);
	CHECK(app->session_options.navigation_source ==
		services.navigation_source);
	CHECK(app->session_options.catalog_source == services.catalog_source);
	CHECK(app->session_options.profile_source == services.profile_source);
	CHECK(app->session_options.palette_source == &palettes);
	CHECK(app->session_options.layout_source == &layouts);
	CHECK(app->session_options.retro_source == services.retro_source);
	CHECK(app->session_options.material_source == services.material_source);
	CHECK(app->session_options.env_sound_source ==
		services.env_sound_source);
	CHECK(app->session_options.horn_source == services.horn_source);
	CHECK(app->session_options.collision_source ==
		services.collision_source);
	CHECK(app->session_options.image_decoder == services.image_decoder);
	CHECK(app->session_options.thumbnail_decoder ==
		services.thumbnail_decoder);
	CHECK(app->session_options.portrait_decoder ==
		services.portrait_decoder);
	CHECK(app->session_options.videos == services.videos);
	CHECK(app->session_options.network ==
		(app->session_options.network_legacy ? services.legacy_network
						: services.network));
	CHECK(app->capture == rendering.capture);
	CHECK(app->session_options.capture == rendering.capture);
	CHECK(app->session_options.screenshot_data == services.screenshot_data);
	services.network = NULL;
	services.videos = NULL;
	services.audio = NULL;
	CHECK(app->inputs == &input.provider &&
		app->session_options.inputs == &input.provider);
	rendering.renderers = NULL;
	rendering.canvases = NULL;
	rendering.windows = NULL;
	services.inputs = NULL;
	services.clock = NULL;
	services.storage = NULL;
	services.user_data = NULL;
	services.resources = NULL;
	services.resource_archives = NULL;
	steps = retried = retry_samples = edited = 0;
	saved.data = NULL;
	saved.size = 0;

	do {
		status = mh_application_step(app);

		if (status == MH_APP_FAILED &&
			mh_application_storage_error(app, NULL) !=
				MH_ERROR_NONE) {
			CHECK(!fail && !retried &&
				resource_state.data_writes == 1);
			CHECK(mh_user_data_read(resource_state.user_data,
				"Saves/motorhead.cfg",
				MH_SETTINGS_TEXT_CAPACITY, &saved,
				NULL) == MH_ERROR_NONE);
			CHECK(saved.size != 0);
			CHECK(check_picture(resource_state.user_data) == 0);
			retry_samples = fixture.samples;
			CHECK(mh_application_retry_storage(app, NULL) ==
				MH_ERROR_NONE);
			retried = 1;
			status = MH_APP_RUNNING;
		}

		if (!fail && !edited && app->menus != NULL &&
			app->menus->controls_store != NULL) {
			struct mh_control_catalog *catalog;
			struct mh_control_profile *profile;

			catalog = &app->menus->controls_catalog;
			profile = &catalog->profiles[catalog->selected];
			CHECK(profile->layout.bindings[0] == 0x14);
			profile->layout.bindings[0] = 0x2c;
			profile->dirty = 1;
			CHECK(mh_menus_controls_save(app->menus, NULL) ==
				MH_ERROR_NONE);
			CHECK(profile->dirty == 0);
			CHECK(mh_menus_controls_reload(app->menus, NULL) ==
				MH_ERROR_NONE);
			CHECK(catalog->profiles[catalog->selected]
					.layout.bindings[0] == 0x2c);
			CHECK(check_memory_rankings(app->menus,
				resource_state.user_data) == 0);
			CHECK(check_memory_leagues(app->menus,
				resource_state.user_data) == 0);
			CHECK(save_memory_picture(app) == 0);
			edited = 1;
		}

		steps++;
		CHECK(steps < 20);
	} while (status == MH_APP_RUNNING);

	CHECK(status == (fail ? MH_APP_FAILED : MH_APP_DONE));
	CHECK(resource_state.ready && resource_state.premature == 0 &&
		resource_state.reads > 0);

	CHECK(resource_state.table_reads == 0);

	if (!fail) {
		CHECK(tables->layout_reads == 1 && tables->palette_reads == 1);
		CHECK(memcmp(&app->menus->layout, &tables->layout,
			sizeof(tables->layout)) == 0);

		for (color = 0; color < MH_MENU_PERSONAL_COLORS; color++)
			CHECK(app->menus->personal_colors[color] ==
				tables->colors[MH_PALETTE_COLORS - 1 - color]);

		CHECK(fixture.samples > 2 && fixture.ticks > 0 &&
			resource_state.ranges > 0 &&
			resource_state.menu_sounds == 1);
		CHECK(edited && retried && fixture.samples == retry_samples);
		CHECK(resource_state.data_reads == 3);
		CHECK(resource_state.data_writes == 1);
		CHECK(resource_state.directories == 0);
		CHECK(resource_state.directory_creates == 11);
		CHECK(resource_state.directory_opens == 5);
		CHECK(fixture.calendar_reads == 2);
		CHECK(resource_state.directory_checks == 2);

		/*
		 * Control catalog open/reload and the installed demo catalog.
		 */
		CHECK(resource_state.resource_directories == 3);
		CHECK(resource_state.data_browses == 1);
		CHECK(strcmp(app->menus->progress_directory, "Saves/") == 0);
		CHECK(strcmp(app->menus->recording_directory, "Saves/") == 0);
		CHECK(check_memory_recordings(app->menus) == 0);
		CHECK(app->menus->network == app->session_options.network);
		CHECK(check_picture(resource_state.user_data) == 0);
		CHECK(mh_user_data_check_directory(resource_state.user_data,
			"Saves/Progress", NULL) == MH_ERROR_NOT_FOUND);
		book = mh_address_store_book(app->menus->address_store);
		CHECK(book != NULL && book->count == 2);
		CHECK(strcmp(book->entries[1].profile.ip, "memory.example") ==
			0);
		CHECK(mh_user_data_read(resource_state.user_data,
			"Saves/motorhead.cfg", MH_SETTINGS_TEXT_CAPACITY,
			&after, NULL) == MH_ERROR_NONE);
		CHECK(after.size == saved.size &&
			memcmp(after.data, saved.data, saved.size) == 0);
		free(saved.data);
		free(after.data);
	}

	if (fail == HOST_DIRECTORY_FAILURE)
		CHECK(resource_state.directory_opens == 2 &&
			resource_state.directory_creates == 5);

	if (fail == HOST_CONTROL_FAILURE)
		CHECK(resource_state.directory_opens == 1 &&
			resource_state.directory_creates == 3);

	samples = fixture.samples;
	tick_reads = fixture.ticks;
	CHECK(mh_application_close(app) == (fail ? 1 : 0));
	CHECK(fixture.samples == samples && fixture.ticks == tick_reads);
	CHECK(input.opened == (fail ? 0 : 1) && input.closed == input.opened);

	if (!fail)
		CHECK(input.events > 0 && input.samples > 0 &&
			input.layouts > 0);

	CHECK(canvas_state.opened == (fail ? 0 : 1) &&
		canvas_state.closed == canvas_state.opened);

	if (!fail)
		CHECK(canvas_state.drawn > 0 &&
			canvas_state.presented == canvas_state.drawn);

	CHECK(mh_window_set_title(host_window, "Host still owns video", NULL) ==
		MH_ERROR_NONE);
	mh_window_destroy(host_window);
	mh_window_system_release(windows);
	mh_memory_user_data_close(resource_state.user_data);
	free(tables);

	return 0;
}

int
main(int argc, char *argv[])
{
	CHECK(argc == 1 || argc == 3);
	CHECK(check_arguments() == 0);
	CHECK(check_network_arguments() == 0);
	CHECK(check_services() == 0);

	if (argc == 3) {
		CHECK(window_failure(argv[1], WINDOW_ACQUIRE_FAILURE) == 0);
		CHECK(window_failure(argv[1], WINDOW_OPEN_FAILURE) == 0);
		CHECK(window_failure(argv[1], WINDOW_INCOMPATIBLE) == 0);
		CHECK(check_frames(argv[1], argv[2]) == 0);
		CHECK(check_host_frames(argv[1], HOST_SUCCEEDS) == 0);
		CHECK(check_host_frames(argv[1], HOST_RESOURCE_FAILURE) == 0);
		CHECK(check_host_frames(argv[1], HOST_DIRECTORY_FAILURE) == 0);
		CHECK(check_host_frames(argv[1], HOST_CONTROL_FAILURE) == 0);
		CHECK(check_host_frames(argv[1], HOST_SUCCEEDS) == 0);
	}

	mh_platform_shutdown();
	puts("Application ownership, cancellation, terminal status and reentry passed");

	return 0;
}
