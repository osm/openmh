#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/resource_pack.h"
#include "../support/software_presentation.h"
#include "../support/wireframe_source.h"
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
#include "backends/sprite/spr_provider.h"
#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "backends/ui/dta_provider.h"
#include "client/application/application.h"
#include "client/application/application_internal.h"
#include "client/menus/internal.h"
#include "engine/storage/storage.h"
#include "interfaces/clock/backend.h"
#include "interfaces/input/backend.h"
#include "interfaces/render/service.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "portable host:%d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct host {
	struct mh_test_presentation	*presentation;
	unsigned long			milliseconds, input_window;
	unsigned int			inputs_opened, inputs_closed;
	unsigned int			prepares, commits, polls, cancels;
	struct mh_event			event;
	int				queued;
	unsigned int			suspends, resumes;
	int				fail_suspend, fail_resume, fail_commit;
};

static enum mh_error_code
set_suspended(void *context, int suspended, struct mh_error *error_out)
{
	struct host *host;

	host = context;

	if ((suspended && host->fail_suspend) ||
		(!suspended && host->fail_resume))
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, "host transition failed");

	/* This host has no asynchronous audio, video or network devices. */
	if (suspended)
		host->suspends++;
	else {
		host->resumes++;
		host->queued = 0;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static struct mh_clock_counter
counter(void *context)
{
	struct host *host;
	struct mh_clock_counter value;

	host = context;
	value.low = host->milliseconds;
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
	return ((struct host *)context)->milliseconds;
}

static enum mh_error_code
calendar(void *context, struct mh_clock_date *date, struct mh_error *error_out)
{
	(void)context;
	date->year = 2026;
	date->month = 10;
	date->day = 25;
	date->hour = 12;
	date->minute = 34;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_storage_status
begin(void *context, enum mh_storage_operation operation,
	struct mh_error *error_out)
{
	struct host *host;

	host = context;

	if (operation == MH_STORAGE_PREPARE)
		host->prepares++;
	else
		host->commits++;
	mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return MH_STORAGE_PENDING;
}

static enum mh_storage_status
poll(void *context, struct mh_error *error_out)
{
	struct host *host;

	host = context;
	host->polls++;

	if (host->fail_commit) {
		mh_error_set(error_out, MH_ERROR_IO, "host commit failed");
		return MH_STORAGE_FAILED;
	}

	mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return MH_STORAGE_READY;
}

static void
cancel(void *context)
{
	((struct host *)context)->cancels++;
}

static enum mh_error_code
mouse_grab(void *context, void *token, int enabled, struct mh_error *error_out)
{
	(void)context;
	(void)token;
	(void)enabled;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_input(void *context, struct mh_window *window, int grab_mouse,
	void **output, struct mh_error *error_out)
{
	struct host *host;

	(void)grab_mouse;
	host = context;
	host->input_window = window == NULL ? 0 : mh_window_id(window);
	host->inputs_opened++;
	*output = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_input(void *context, void *input)
{
	(void)input;
	((struct host *)context)->inputs_closed++;
}

static int
focused(void *context, const void *input)
{
	(void)context;
	(void)input;

	return 1;
}

static int
event(void *context, void *input, struct mh_event *output)
{
	struct host *host;

	(void)input;
	host = context;

	if (!host->queued)
		return 0;

	*output = host->event;
	host->queued = 0;

	return 1;
}

static enum mh_error_code
sample(void *context, void *input, struct mh_keyboard *keyboard,
	const unsigned char down[MH_KEY_COUNT], unsigned long now,
	unsigned long mode, struct mh_device_input *snapshot,
	struct mh_error *error_out)
{
	(void)context;

	(void)input;
	(void)mode;
	memset(snapshot, 0, sizeof(*snapshot));

	return mh_keyboard_poll(keyboard, down, now, error_out);
}

static int
key(void *context, unsigned int scan)
{
	(void)context;
	(void)scan;

	return 0;
}

static void
characters(void *context, const unsigned char down[MH_KEY_COUNT],
	unsigned char output[MH_KEY_COUNT])
{
	(void)context;

	(void)down;
	memset(output, 0, MH_KEY_COUNT);
}

static int
frames(struct host *host, struct mh_application *app, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		host->milliseconds += 16;

		if (mh_application_step(app) != MH_APP_RUNNING) {
			fprintf(stderr, "Host step failed: %s\n", app->error);
			return 1;
		}
	}

	return 0;
}

static int
suspension(struct host *host, struct mh_application *app)
{
	long double before;
	unsigned long frame;
	unsigned int suspends, resumes, drawn;

	suspends = host->suspends;
	resumes = host->resumes;
	host->fail_suspend = 1;
	CHECK(mh_application_set_suspended(app, 1, NULL) == MH_ERROR_BACKEND);
	CHECK(!mh_application_suspended(app));
	host->fail_suspend = 0;
	CHECK(mh_application_set_suspended(app, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_set_suspended(app, 1, NULL) == MH_ERROR_NONE);
	CHECK(host->suspends == suspends + 1);
	frame = app->frame_index;
	drawn = host->presentation->frames;
	before = mh_clock_source_ms(app->timer.source);
	host->milliseconds += 60000;
	CHECK(frames(host, app, 4) == 0);
	CHECK(app->frame_index == frame);
	CHECK(host->presentation->frames == drawn);
	CHECK(mh_clock_source_ms(app->timer.source) == before);
	CHECK(mh_application_checkpoint(app, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_checkpoint(app, NULL) == MH_ERROR_STATE);
	CHECK(mh_application_step(app) == MH_APP_RUNNING);
	host->fail_commit = 1;
	CHECK(mh_application_step(app) == MH_APP_FAILED);
	CHECK(mh_application_storage_error(app, NULL) == MH_ERROR_IO);
	CHECK(mh_application_suspended(app) && app->frame_index == frame);
	host->fail_commit = 0;
	CHECK(mh_application_retry_storage(app, NULL) == MH_ERROR_NONE);
	CHECK(frames(host, app, 4) == 0);
	CHECK(!app->storage_waiting && app->frame_index == frame);
	CHECK(host->presentation->frames == drawn);
	host->fail_resume = 1;
	CHECK(mh_application_set_suspended(app, 0, NULL) == MH_ERROR_BACKEND);
	CHECK(mh_application_suspended(app));
	CHECK(frames(host, app, 4) == 0);
	CHECK(app->frame_index == frame);
	CHECK(host->presentation->frames == drawn);
	host->fail_resume = 0;
	CHECK(mh_application_set_suspended(app, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_set_suspended(app, 0, NULL) == MH_ERROR_NONE);
	CHECK(host->resumes == resumes + 1);
	CHECK(mh_clock_source_ms(app->timer.source) == before);

	return 0;
}

static int
press(struct host *host, struct mh_application *app, unsigned int scan,
	enum mh_shortcut shortcut)
{
	memset(&host->event, 0, sizeof(host->event));

	host->event.type = MH_EVENT_KEY_DOWN;
	host->event.window = host->input_window;
	host->event.scan = scan;
	host->event.shortcut = shortcut;
	host->queued = 1;
	CHECK(frames(host, app, 4) == 0);
	CHECK(!host->queued);
	host->event.type = MH_EVENT_KEY_UP;
	host->queued = 1;
	CHECK(frames(host, app, 40) == 0);
	CHECK(!host->queued);

	return 0;
}

static int
play(struct host *host, struct mh_test_presentation *presentation,
	struct mh_app_services *services)
{
	static const struct mh_storage_ops storage_ops = {begin, poll, cancel};
	struct mh_application *app;
	unsigned int steps, opened, results, width, height;
	unsigned long windows, closed_windows;
	enum mh_app_status status;
	struct mh_file checkpoint;
	char *args[] = {"motorhead", "--debug-no-intro", "--debug-no-records",
		"--audio", "0", "--assets", "memory-assets", "--renderer",
		"software", "--debug-confirmation", "0", "--debug-record", "0"};

	opened = presentation->opened;
	results = presentation->results;
	windows = presentation->windows_opened;
	closed_windows = presentation->windows_closed;
	CHECK(mh_storage_create(&storage_ops, host, &services->storage, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_application_open_with_services(
		(int)(sizeof(args) / sizeof(args[0])), args, services,
		&app, NULL) == MH_ERROR_NONE);
	CHECK(frames(host, app, 50) == 0);
	CHECK(presentation->windows_opened == windows + 1);
	CHECK(presentation->windows_closed == closed_windows);
	CHECK(app->menus != NULL &&
		app->menus->menu.current == &app->menus->main_page);
	CHECK(suspension(host, app) == 0);
	CHECK(mh_window_set_size(app->window, 734, 492, NULL) == MH_ERROR_NONE);
	CHECK(app->settings_store != NULL);
	CHECK(mh_user_data_read(services->user_data,
		mh_settings_store_path(app->settings_store),
		MH_RESOURCE_READ_LIMIT, &checkpoint,
		NULL) == MH_ERROR_NONE);
	CHECK(checkpoint.size > 0);
	free(checkpoint.data);
	CHECK(press(host, app, MH_KEY_ENTER, MH_SHORTCUT_ENTER) == 0);
	CHECK(app->menus->menu.current == &app->menus->single_page);
	CHECK(press(host, app, MH_KEY_ENTER, MH_SHORTCUT_ENTER) == 0);
	CHECK(app->menus->launch != NULL);

	/* Menu and loading canvases close before the race uses their window. */
	CHECK(presentation->opened == opened + 3);
	CHECK(presentation->closed == opened + 2);
	CHECK(mh_window_get_size(app->window, &width, &height, NULL) ==
		MH_ERROR_NONE);
	CHECK(width == app->menus->settings->av.width &&
		height == app->menus->settings->av.height);
	CHECK(presentation->windows_opened == windows + 1);
	CHECK(presentation->windows_closed == closed_windows);
	CHECK(frames(host, app, 850) == 0);
	CHECK(suspension(host, app) == 0);
	CHECK(press(host, app, MH_KEY_ESCAPE, MH_SHORTCUT_ESCAPE) == 0);
	CHECK(press(host, app, MH_KEY_UP, MH_SHORTCUT_NONE) == 0);
	CHECK(press(host, app, MH_KEY_ENTER, MH_SHORTCUT_ENTER) == 0);

	/* Patched Single aborts return directly, without a results board. */
	CHECK(presentation->results == results);
	CHECK(app->menus->launch == NULL && app->menus->dialog_kind == 0);
	CHECK(app->menus->menu.current == &app->menus->main_page);
	CHECK(presentation->opened == presentation->closed + 1);
	CHECK(presentation->windows_opened == windows + 1);
	CHECK(presentation->windows_closed == closed_windows);
	CHECK(host->input_window == mh_window_id(app->window));
	CHECK(mh_window_get_size(app->window, &width, &height, NULL) ==
		MH_ERROR_NONE);
	CHECK(width == 734 && height == 492);

	host->event.type = MH_EVENT_QUIT;
	host->queued = 1;
	status = MH_APP_RUNNING;

	for (steps = 0; steps < 20 && status == MH_APP_RUNNING; steps++) {
		host->milliseconds += 16;
		status = mh_application_step(app);
	}

	CHECK(status == MH_APP_DONE);
	CHECK(mh_application_close(app) == 0);
	CHECK(presentation->windows_closed == closed_windows + 1);
	CHECK(host->commits > 0 && host->polls > host->prepares);
	CHECK(presentation->opened == presentation->closed);
	CHECK(presentation->references == 1 && presentation->pixels == NULL);
	puts("Portable host: memory assets, menus, race and aborted-race return passed");

	return 0;
}

int
main(int argc, char *argv[])
{
	static const struct mh_clock_ops clock_ops = {
		counter, frequency, ticks, NULL, calendar};

	static const struct mh_storage_ops storage_ops = {begin, poll, cancel};
	static const struct mh_input_ops input_ops = {open_input, close_input,
		focused, event, sample, key, characters, NULL, mouse_grab};

	struct host host;
	struct mh_test_resource_pack pack;
	struct mh_test_wireframe_source wireframes;
	struct mh_wireframe_source wireframe_source;
	size_t i;
	struct mh_test_presentation presentation;
	struct mh_input_provider inputs;
	struct mh_clock_source clock;
	struct mh_render_set renderers;
	struct mh_app_services services;
	struct mh_app_lifecycle lifecycle;
	struct mh_render_service rendering;
	struct mh_application *app;
	enum mh_app_status status;
	unsigned int steps;
	char *help[] = {"motorhead", "--help"};
	char *missing[] = {"motorhead", "--debug-no-intro", "--audio", "0",
		"--assets", "memory-assets"};

	CHECK(argc == 1 || argc == 2);
	memset(&pack, 0, sizeof(pack));
	memset(&wireframes, 0, sizeof(wireframes));
	mh_test_wireframe_source_init(&wireframes, &wireframe_source);
	memset(&host, 0, sizeof(host));
	memset(&services, 0, sizeof(services));
	memset(&rendering, 0, sizeof(rendering));
	CHECK(mh_test_presentation_open(&presentation, NULL) == MH_ERROR_NONE);
	host.presentation = &presentation;
	CHECK(mh_input_provider_init(&inputs, &input_ops, &host, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_clock_source_init(&clock, &clock_ops, &host, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_resources_open(NULL, 0, &services.resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&services.user_data, NULL) ==
		MH_ERROR_NONE);
	renderers.glide = NULL;
	renderers.direct3d = NULL;
	renderers.powervr = NULL;
	renderers.software = renderers.ui = &presentation.backend;
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
	rendering.renderers = &renderers;
	rendering.windows = &presentation.windows;
	rendering.canvases = &presentation.canvases;
	services.inputs = &inputs;
	services.clock = &clock;
	lifecycle.context = &host;
	lifecycle.set_suspended = set_suspended;
	services.lifecycle = &lifecycle;

	CHECK(mh_storage_create(&storage_ops, &host, &services.storage, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_application_open_with_services(
		2, help, &services, &app, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_step(app) == MH_APP_DONE);
	CHECK(mh_application_close(app) == 0);
	CHECK(host.prepares == 0 && host.commits == 0);

	services.lifecycle = NULL;
	CHECK(mh_storage_create(&storage_ops, &host, &services.storage, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_application_open_with_services(
		(int)(sizeof(missing) / sizeof(missing[0])), missing,
		&services, &app, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_set_suspended(app, 1, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(!mh_application_suspended(app));
	CHECK(mh_application_close(app) != 0);
	services.lifecycle = &lifecycle;

	CHECK(mh_storage_create(&storage_ops, &host, &services.storage, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_application_open_with_services(
		(int)(sizeof(missing) / sizeof(missing[0])), missing,
		&services, &app, NULL) == MH_ERROR_NONE);
	CHECK(app->capture == NULL &&
		app->session_options.screenshot_data == NULL);
	CHECK(app->session_options.network == NULL &&
		app->session_options.videos == NULL);
	CHECK(mh_application_set_suspended(app, 1, NULL) == MH_ERROR_NONE);
	CHECK(frames(&host, app, 4) == 0);
	CHECK(mh_application_set_suspended(app, 0, NULL) == MH_ERROR_NONE);
	CHECK(suspension(&host, app) == 0);
	status = MH_APP_RUNNING;

	for (steps = 0; steps < 20 && status == MH_APP_RUNNING; steps++) {
		host.milliseconds += 16;
		status = mh_application_step(app);
	}

	CHECK(status == MH_APP_FAILED);
	CHECK(host.prepares == 1 && host.polls >= 1);
	CHECK(mh_application_close(app) != 0);
	CHECK(presentation.references == 1 && presentation.pixels == NULL);
	CHECK(presentation.opened == presentation.closed);

	if (argc == 2) {
		mh_memory_resources_close(services.resources);
		CHECK(mh_test_resource_pack_load(&pack, argv[1]));

		for (i = 0; i < pack.count; i++)
			CHECK(mh_test_wireframe_convert(&wireframes,
				&pack.entries[i], NULL) == MH_ERROR_NONE);

		CHECK(wireframes.count > 0);
		services.wireframe_source = &wireframe_source;
		CHECK(mh_memory_resources_open(pack.entries, pack.count,
			&services.resources, NULL) == MH_ERROR_NONE);
		CHECK(play(&host, &presentation, &services) == 0);
		CHECK(wireframes.reads > 0);
		printf("Alternate menu wireframes: %lu templates, %lu loads; no LOB resources remain\n",
			(unsigned long)wireframes.count,
			(unsigned long)wireframes.reads);
	}

	CHECK(mh_storage_create(&storage_ops, &host, &services.storage, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_application_open_with_services(
		(int)(sizeof(missing) / sizeof(missing[0])), missing,
		&services, &app, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_set_suspended(app, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_step(app) == MH_APP_RUNNING);
	steps = host.resumes;
	CHECK(mh_application_close(app) != 0);
	CHECK(host.resumes == steps);

	mh_memory_user_data_close(services.user_data);
	mh_memory_resources_close(services.resources);
	mh_test_resource_pack_free(&pack);
	mh_test_wireframe_source_free(&wireframes);
	mh_test_presentation_close(&presentation);
	CHECK(presentation.references == 0);
	CHECK(presentation.windows_opened == presentation.windows_closed);
	CHECK(host.inputs_opened == host.inputs_closed);
	puts("Portable host: help, deferred preparation, missing resources and teardown passed");

	return 0;
}
