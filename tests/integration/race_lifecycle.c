#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/input_provider.h"
#include "backends/audio/provider.h"
#include "backends/audio/sdl_provider.h"
#include "backends/image/tga_provider.h"
#include "backends/retro/provider.h"
#include "backends/storage/memory_user_data.h"
#include "backends/window/sdl_provider.h"
#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/render_renderer.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/internal.h"
#include "engine/core/error.h"
#include "engine/render/renderer.h"
#include "interfaces/audio/backend.h"
#include "interfaces/image/encoder_backend.h"
#include "interfaces/input/backend.h"
#include "interfaces/window/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "race lifecycle line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

enum {
	SCREENSHOT_SCAN = 0x58
};

static struct mh_race_context race_context;
static struct mh_field field;
static const char shadow_error[] = "injected software shadow setup failure";

struct fixture {
	unsigned int	calls;
	int		fail;
};

struct renderer_fixture {
	const struct mh_render_backend	*native;
	unsigned int			opens, destroys;
	int				fail_shadows, shadow_error_owned;
};

struct focus_pause_case {
	int	external_ui, continuous, attract, paused_on_return;
};

static enum mh_error_code
open_renderer(const void *context, struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out)
{
	struct renderer_fixture *fixture;

	fixture = (struct renderer_fixture *)context;
	fixture->opens++;

	return fixture->native->open(fixture->native->context, window, meshes,
		count, width, height, flags, output, error_out);
}

static int
mesh_shadows(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	struct renderer_fixture *fixture;

	fixture = (struct renderer_fixture *)renderer->backend->context;

	if (fixture->fail_shadows) {
		mh_error_set(error_out, MH_ERROR_FAILED, shadow_error);
		return 0;
	}

	return fixture->native->ops->mesh_shadows(renderer, index, error_out);
}

static void
destroy_renderer(struct mh_renderer *renderer)
{
	struct renderer_fixture *fixture;

	fixture = (struct renderer_fixture *)renderer->backend->context;
	fixture->destroys++;

	if (fixture->fail_shadows)
		fixture->shadow_error_owned =
			renderer->error.message == renderer->error_message &&
			strcmp(renderer->error.message, shadow_error) == 0;

	fixture->native->ops->destroy(renderer);
}

static enum mh_error_code
advance(void *context, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = (struct fixture *)context;
	fixture->calls++;
	*done = fixture->calls == 3;

	if (fixture->fail)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"injected lifecycle failure");

	return mh_field_advance(
		&field, controls, elapsed, action, steps, error_out);
}

static enum mh_error_code
start(struct mh_error *error_out)
{
	struct mh_drive drive;
	enum mh_error_code status;

	status = mh_drive_start(&race_context.scene.assets.car,
		&race_context.scene.assets.world_collision,
		&race_context.scene.assets.materials,
		&race_context.scene.assets.route,
		&race_context.scene.assets.track, 0, 0, 1, &drive, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_start(
			&drive, NULL, 0, NULL, 0, &field, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_hulls(&field,
			race_context.scene.assets.car_hulls, field.count,
			error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_race_scene_bind_objects(
		&race_context.scene, &field, error_out);
}

static enum mh_error_code
encode_sample(void *context, const struct mh_picture *picture,
	struct mh_file *output, struct mh_error *error_out)
{
	struct mh_file bytes;
	int *fail;

	fail = context;

	if (*fail)
		return mh_error_set(
			error_out, MH_ERROR_IO, "test encoder failure");

	if (picture->format != MH_PICTURE_RGBA8 || picture->pixel_size < 4)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"test encoder requires RGBA8");

	bytes.data = malloc(4);

	if (bytes.data == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test allocation");

	bytes.size = 4;
	memcpy(bytes.data, picture->pixels, 4);
	*output = bytes;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
capture_picture(struct mh_race_loop *loop, const struct mh_user_data *data)
{
	struct mh_screenshot state;
	struct mh_screenshot_result result;
	struct mh_binding_input input;
	struct mh_file picture, expected;
	struct mh_image image;
	struct mh_image_encoder encoder;
	int fail;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	input.down[SCREENSHOT_SCAN] = MH_KEY_DOWN_BIT;
	CHECK(loop->screenshot.user_data == data);
	CHECK(loop->screenshot.store == NULL);
	CHECK(mh_user_data_check_directory(data, "Screenshots", NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(mh_screenshot_capture_poll(&loop->screenshot, &state,
		MH_BINDING_KEYBOARD | SCREENSHOT_SCAN, &input, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.saved && strcmp(result.name, "motor0.tga") == 0);
	CHECK(mh_user_data_read(data, "Screenshots/motor0.tga", 32000800UL,
		&picture, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(loop->renderer, &expected));
	CHECK(picture.size == expected.size &&
		memcmp(picture.data, expected.data, picture.size) == 0);
	free(picture.data);
	free(expected.data);

	/* Exercise provider dispatch, readback, retry and storage together. */
	encoder.extension = "sample";
	encoder.context = &fail;
	encoder.encode = encode_sample;
	loop->screenshot.encoder = &encoder;
	state.latched = 0;
	fail = 1;
	CHECK(mh_screenshot_capture_poll(&loop->screenshot, &state,
		MH_BINDING_KEYBOARD | SCREENSHOT_SCAN, &input, &result,
		NULL) == MH_ERROR_IO);
	CHECK(!state.latched && loop->screenshot.store == NULL);
	fail = 0;
	CHECK(mh_screenshot_capture_poll(&loop->screenshot, &state,
		MH_BINDING_KEYBOARD | SCREENSHOT_SCAN, &input, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.saved && strcmp(result.name, "motor0.sample") == 0);
	CHECK(mh_user_data_read(data, "Screenshots/motor0.sample", 4, &picture,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_read_pixels(loop->renderer, &image, NULL) ==
		MH_ERROR_NONE);
	CHECK(picture.size == 4 && memcmp(picture.data, image.rgba, 4) == 0);
	free(picture.data);
	free(image.rgba);
	loop->screenshot.encoder = mh_image_tga_encoder();

	return 0;
}

static int
unbound_race_keys(struct mh_race_loop *loop, struct mh_test_input *input)
{
	static const unsigned int scans[] = {0x13, MH_KEY_HOME, 0x19, 0x21,
		0x32, 0x22, 0x31, MH_KEY_Q, MH_KEY_E, MH_KEY_TAB};

	static const enum mh_shortcut shortcuts[] = {MH_SHORTCUT_R,
		MH_SHORTCUT_HOME, MH_SHORTCUT_P, MH_SHORTCUT_F, MH_SHORTCUT_M,
		MH_SHORTCUT_G, MH_SHORTCUT_N, MH_SHORTCUT_Q, MH_SHORTCUT_E,
		MH_SHORTCUT_TAB};

	static struct mh_field saved;
	unsigned int i, mode, rate, map, gaps, names;
	int continuous, paused, focused, reflections;

	CHECK(mh_race_poll_window_events(loop));
	continuous = loop->continuous;
	paused = loop->paused;
	focused = loop->focused;
	rate = loop->tick_clock.rate;
	map = loop->map_mode;
	gaps = loop->gap_mode;
	names = loop->names_mode;
	reflections = loop->reflections;
	loop->paused = 0;
	loop->focused = 1;
	saved = *loop->field;

	/*
	 * Local/network and legacy/fixed-tick input must have no extra actions.
	 */
	for (mode = 0; mode < 4; mode++) {
		loop->continuous = mode & 1;
		loop->tick_clock.rate = mode & 2 ? 120 : 0;

		for (i = 0; i < sizeof(scans) / sizeof(scans[0]); i++) {
			memset(&input->event, 0, sizeof(input->event));
			input->event.type = MH_EVENT_KEY_DOWN;
			input->event.window = mh_window_id(loop->window);
			input->event.scan = scans[i];
			input->event.shortcut = shortcuts[i];
			input->event.shift = scans[i] == MH_KEY_TAB;
			input->pending = 1;
			CHECK(mh_race_poll_window_events(loop));
			CHECK(loop->down[scans[i]] == MH_KEY_DOWN_BIT);
			CHECK(!loop->paused && !loop->screen.visible);
			CHECK(loop->reflections == reflections &&
				loop->map_mode == map &&
				loop->gap_mode == gaps &&
				loop->names_mode == names);
			CHECK(memcmp(loop->field, &saved, sizeof(saved)) == 0);
			input->event.type = MH_EVENT_KEY_UP;
			input->pending = 1;
			CHECK(mh_race_poll_window_events(loop));
			CHECK(loop->down[scans[i]] == 0);
		}
	}

	loop->continuous = continuous;
	loop->paused = paused;
	loop->focused = focused;
	loop->tick_clock.rate = rate;

	return 0;
}

static int
focus_pause_policy(struct mh_race_loop *loop, struct mh_test_input *input)
{
	static const struct focus_pause_case cases[] = {
		{0, 0, 0, 1}, {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};

	const struct mh_race_session_hooks *original;
	struct mh_race_session_hooks hooks;
	struct mh_window *window;
	unsigned int i;
	int paused, focused, continuous, attract;

	original = loop->session;
	window = loop->window;

	/* Headless rendering has no window, but focus events need an ID. */
	if (window == NULL)
		CHECK(mh_window_open(loop->windows, "Focus policy", 64, 64,
			MH_WINDOW_HIDDEN, &loop->window,
			NULL) == MH_ERROR_NONE);

	hooks = *original;
	paused = loop->paused;
	focused = loop->focused;
	continuous = loop->continuous;
	attract = loop->field->attract;
	loop->session = &hooks;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		hooks.external_ui = cases[i].external_ui;
		loop->continuous = cases[i].continuous;
		loop->field->attract = cases[i].attract;
		loop->paused = 0;
		loop->focused = 1;
		loop->down[MH_KEY_UP] = MH_KEY_DOWN_BIT;
		memset(&input->event, 0, sizeof(input->event));
		input->event.window = mh_window_id(loop->window);
		input->event.type = MH_EVENT_FOCUS_LOST;
		input->pending = 1;
		CHECK(mh_race_poll_window_events(loop));
		CHECK(!loop->focused && !loop->down[MH_KEY_UP]);
		CHECK(loop->paused ==
			(!cases[i].external_ui && !cases[i].continuous));
		input->event.type = MH_EVENT_FOCUS_GAINED;
		input->pending = 1;
		CHECK(mh_race_poll_window_events(loop));
		CHECK(loop->focused &&
			loop->paused == cases[i].paused_on_return);
	}

	loop->session = original;
	loop->paused = paused;
	loop->focused = focused;
	loop->continuous = continuous;
	loop->field->attract = attract;

	if (window == NULL)
		mh_window_destroy(loop->window);

	loop->window = window;

	return 0;
}

static int
exercise(const char *root)
{
	struct mh_test_input input;
	struct mh_render_set renderers;
	struct mh_render_backend backend;
	struct mh_render_ops ops;
	struct mh_error error, retained;
	char retained_text[512];
	struct renderer_fixture provider;
	unsigned int opens, destroys;
	struct mh_race_session_hooks session;
	struct mh_race_loop_options options;
	struct mh_race_loop *loop, *unchanged;
	struct mh_audio_options audio;
	const struct mh_user_data *data;
	struct fixture fixture;
	enum mh_app_status status;
	unsigned int phase, scenario, frame;

	CHECK(mh_test_input_init(&input, NULL) == MH_ERROR_NONE);
	memset(&session, 0, sizeof(session));
	memset(&options, 0, sizeof(options));
	renderers = *mh_diagnostic_renderers();
	memset(&provider, 0, sizeof(provider));
	provider.native = mh_render_set_profile(
		&renderers, race_context.scene.assets.profile);
	backend = *provider.native;
	backend.context = &provider;
	backend.open = open_renderer;
	ops = *backend.ops;
	ops.mesh_shadows = mesh_shadows;
	ops.destroy = destroy_renderer;
	backend.ops = &ops;

	if (backend.profile == MH_RENDER_SOFTWARE)
		renderers.software = &backend;
	else if (backend.profile == MH_RENDER_DIRECT3D)
		renderers.direct3d = &backend;
	else if (backend.profile == MH_RENDER_POWERVR)
		renderers.powervr = &backend;
	else
		renderers.glide = &backend;
	options.renderers = &renderers;
	options.windows = mh_sdl_window_system();
	options.inputs = &input.provider;
	options.audio_provider = mh_sdl_audio_provider();
	options.retro_source = mh_retro_fallback_source();
	options.audio_decoder = mh_audio_fallback_decoder();
	options.audio_trace = 0;
	options.timer = mh_game_clock_timer();
	memset(&audio, 0, sizeof(audio));
	session.context = &fixture;
	session.pause_with_ui = 1;
	session.advance = advance;
	options.headless = 1;
	options.root = root;
	options.session = &session;

	/*
	 * Failed initialization must leave no open state or output ownership.
	 */
	CHECK(start(NULL) == MH_ERROR_NONE);
	loop = NULL;
	CHECK(mh_race_loop_open(&race_context, &field, &options, &loop, NULL) !=
		MH_ERROR_NONE);
	CHECK(loop == NULL);
	options.audio_options = &audio;
	input.fail_open = 1;
	CHECK(mh_race_loop_open(&race_context, &field, &options, &loop, NULL) !=
		MH_ERROR_NONE);
	CHECK(loop == NULL && input.opened == 0 && input.closed == 0);
	input.fail_open = 0;

	if (backend.profile == MH_RENDER_SOFTWARE) {
		/* The setup diagnostic must survive renderer destruction. */
		opens = provider.opens;
		destroys = provider.destroys;
		provider.fail_shadows = 1;
		CHECK(mh_race_loop_open(&race_context, &field, &options, &loop,
			&error) == MH_ERROR_FAILED);
		CHECK(error.code == MH_ERROR_FAILED &&
			strcmp(error.message, shadow_error) == 0);
		CHECK(loop == NULL && input.opened == 0 && input.closed == 0);
		CHECK(provider.opens == opens + 1 &&
			provider.destroys == destroys + 1);
		CHECK(provider.shadow_error_owned);
		provider.fail_shadows = 0;
	}

	/*
	 * Completion, early close and failure in legacy and fixed-tick modes.
	 */
	for (phase = 0; phase < 6; phase++) {
		scenario = phase % 3;
		CHECK(start(NULL) == MH_ERROR_NONE);

		/*
		 * Benchmark timing supplies exactly one fixed tick per frame.
		 */
		session.benchmark = phase >= 3;

		if (session.benchmark)
			CHECK(mh_field_simulation_rate(&field, 120, NULL) ==
				MH_ERROR_NONE);

		memset(&fixture, 0, sizeof(fixture));
		fixture.fail = scenario == 2;
		CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
		options.user_data = data;
		options.screenshot_encoder = mh_image_tga_encoder();
		opens = provider.opens;
		CHECK(mh_race_loop_open(&race_context, &field, &options, &loop,
			NULL) == MH_ERROR_NONE);
		CHECK(fixture.calls == 0);
		CHECK(loop->inputs == &input.provider &&
			input.opened == phase + 1);
		CHECK(provider.opens == opens + 1 &&
			loop->renderer->backend == &backend);
		unchanged = loop;
		CHECK(mh_race_loop_open(&race_context, &field, &options,
			&unchanged, NULL) != MH_ERROR_NONE);
		CHECK(unchanged == loop);
		CHECK(unbound_race_keys(loop, &input) == 0);
		CHECK(focus_pause_policy(loop, &input) == 0);

		status = MH_APP_RUNNING;

		for (frame = 0; frame < (scenario == 0 ? 3U : 1U); frame++) {
			status = mh_race_loop_step(loop);
			CHECK(fixture.calls == frame + 1);
			CHECK(status ==
				(scenario == 2		     ? MH_APP_FAILED
						: frame == 2 ? MH_APP_DONE
							: MH_APP_RUNNING));
			CHECK(loop->field == &field && !loop->in_tick);

			if (session.benchmark && status == MH_APP_RUNNING)
				CHECK(loop->tick_clock.ticks == frame + 1);
		}

		if (scenario != 2)
			CHECK(capture_picture(loop, data) == 0);

		if (scenario != 1) {
			/*
			 * Terminal steps cannot acquire input or advance the
			 * game again.
			 */
			CHECK(mh_race_loop_step(loop) == status);
			CHECK(fixture.calls == (scenario == 0 ? 3U : 1U));
			CHECK(mh_race_loop_delay(loop) == 0);
		}

		CHECK(mh_race_loop_error(loop, &error) ==
			(scenario == 2 ? MH_ERROR_FAILED : MH_ERROR_NONE));

		if (scenario == 2)
			CHECK(strcmp(error.message,
				"injected lifecycle failure") == 0);

		mh_error_copy(&retained, &error, retained_text,
			sizeof(retained_text));
		CHECK(mh_race_loop_close(loop) == (scenario != 2));

		if (scenario == 2)
			CHECK(retained.code == MH_ERROR_FAILED &&
				strcmp(retained.message,
					"injected lifecycle failure") == 0);

		CHECK(input.closed == input.opened);
		mh_memory_user_data_close(data);
	}

	CHECK(provider.opens == provider.destroys);
	CHECK(input.events > 0 && input.samples > 0);
	CHECK(mh_race_loop_error(NULL, NULL) == MH_ERROR_INVALID);
	CHECK(mh_race_loop_step(NULL) == MH_APP_FAILED);
	CHECK(mh_race_loop_close(NULL) == 0);
	puts("Race lifecycle: failed open, legacy/fixed ticks, completion, early close, failure, authoritative field restoration, memory screenshots and reentry passed");

	return 0;
}

int
main(int argc, char *argv[])
{
	struct mh_error error;
	int result;
	enum mh_render_profile profile;
	enum mh_error_code status;

	if (argc != 2 && argc != 3)
		return 2;

	profile = MH_RENDER_GLIDE;

	if (argc == 3) {
		if (strcmp(argv[2], "software") == 0)
			profile = MH_RENDER_SOFTWARE;
		else if (strcmp(argv[2], "powervr") == 0)
			profile = MH_RENDER_POWERVR;
		else if (strcmp(argv[2], "direct3d") == 0)
			profile = MH_RENDER_DIRECT3D;
	}

	status = mh_race_context_load_profile(
		argv[1], 1, 1, 1, NULL, profile, NULL, &race_context, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_context_load_hud(
			argv[1], &race_context, &error);

	if (status == MH_ERROR_NONE)
		status = mh_results_assets_load(argv[1], 1, "Lifecycle",
			mh_race_context_atlas(&race_context),
			&race_context.results, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	result = status != MH_ERROR_NONE ? 1 : exercise(argv[1]);
	mh_race_context_free(&race_context);

	return result;
}
