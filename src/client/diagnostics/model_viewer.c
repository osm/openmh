#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/window/sdl_provider.h"
#include "client/diagnostics/input.h"
#include "client/diagnostics/model_viewer.h"
#include "client/diagnostics/render_renderer.h"
#include "client/hosts/clock.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "interfaces/clock/clock.h"
#include "interfaces/input/input.h"
#include "interfaces/render/renderer.h"
#include "interfaces/window/window.h"

#define VIEW_HALF_FOV 0.38
#define VIEW_NEAR_PLANE 0.05
#define VIEW_FAR_PLANE 20.0
#define VIEW_DEFAULT_DISTANCE 2.8
#define VIEW_MOUSE_ORBIT_SCALE 0.01
#define VIEW_WHEEL_ZOOM_STEP 0.2
#define VIEW_MAX_INPUT_STEP 0.1
#define VIEW_MAX_PITCH 1.4
#define VIEW_MIN_DISTANCE 1.5
#define VIEW_MAX_DISTANCE 8.0

#define VIEW_FRAME_DELAY_MS 8

struct viewer_state {
	double	yaw, pitch, distance, animation_seconds;
	int	running, reflections, paused, time_changed;
};

/*
 * Right-handed orbit view and perspective, depth mapped to [0,1]. The
 * renderer backend owns viewport Y conversion; these matrices need no flip.
 */
static void
orbit_matrices(float view_projection[16], float view_matrix[16], double yaw,
	double pitch, double distance)
{
	double view[16];
	double projection[16];
	double value;
	double scale;
	int row;
	int column;
	int k;

	memset(view, 0, sizeof(view));
	view[0] = cos(yaw);
	view[8] = -sin(yaw);
	view[1] = -sin(pitch) * sin(yaw);
	view[5] = cos(pitch);
	view[9] = -sin(pitch) * cos(yaw);
	view[2] = cos(pitch) * sin(yaw);
	view[6] = sin(pitch);
	view[10] = cos(pitch) * cos(yaw);
	view[14] = -distance;
	view[15] = 1.0;

	for (k = 0; k < 16; k++)
		view_matrix[k] = (float)view[k];

	memset(projection, 0, sizeof(projection));
	scale = 1.0 / tan(VIEW_HALF_FOV);
	projection[0] = scale * MH_VIEW_HEIGHT / MH_VIEW_WIDTH;
	projection[5] = scale;
	projection[10] = VIEW_FAR_PLANE / (VIEW_NEAR_PLANE - VIEW_FAR_PLANE);
	projection[11] = -1.0;
	projection[14] = VIEW_NEAR_PLANE * VIEW_FAR_PLANE /
		(VIEW_NEAR_PLANE - VIEW_FAR_PLANE);

	for (column = 0; column < 4; column++) {
		for (row = 0; row < 4; row++) {
			value = 0.0;

			for (k = 0; k < 4; k++) {
				value += projection[k * 4 + row] *
					view[column * 4 + k];
			}

			view_projection[column * 4 + row] = (float)value;
		}
	}
}

static void
handle_event(struct viewer_state *state, const struct mh_event *event,
	const struct mh_model_viewer_options *options)
{
	if (event->type == MH_EVENT_QUIT ||
		(event->type == MH_EVENT_KEY_DOWN &&
			event->shortcut == MH_SHORTCUT_ESCAPE)) {
		state->running = 0;
	} else if (event->type == MH_EVENT_MOUSE_MOTION && event->mouse_left) {
		state->yaw -= event->mouse_x * VIEW_MOUSE_ORBIT_SCALE;
		state->pitch += event->mouse_y * VIEW_MOUSE_ORBIT_SCALE;
	} else if (event->type == MH_EVENT_MOUSE_WHEEL) {
		state->distance -= event->wheel_y * VIEW_WHEEL_ZOOM_STEP;
	} else if (event->type == MH_EVENT_KEY_DOWN &&
		event->shortcut == MH_SHORTCUT_R) {
		state->yaw = options->yaw;
		state->pitch = options->pitch;
		state->distance = VIEW_DEFAULT_DISTANCE;
	} else if (event->type == MH_EVENT_KEY_DOWN &&
		event->shortcut == MH_SHORTCUT_E && !event->repeat) {
		state->reflections = !state->reflections;
	} else if (event->type == MH_EVENT_KEY_DOWN &&
		event->shortcut == MH_SHORTCUT_SPACE && !event->repeat &&
		options->capture_path == NULL) {
		state->paused = !state->paused;
	} else if (event->type == MH_EVENT_KEY_DOWN &&
		event->shortcut == MH_SHORTCUT_HOME && !event->repeat &&
		options->capture_path == NULL) {
		state->animation_seconds = 0.0;
		state->time_changed = 1;
	}
}

static void
update_orbit(struct viewer_state *state, double elapsed_seconds)
{
	if (elapsed_seconds > VIEW_MAX_INPUT_STEP) {
		elapsed_seconds = VIEW_MAX_INPUT_STEP;
	}

	state->yaw += elapsed_seconds *
		(mh_key_down(MH_KEY_RIGHT) - mh_key_down(MH_KEY_LEFT));

	state->pitch += elapsed_seconds *
		(mh_key_down(MH_KEY_UP) - mh_key_down(MH_KEY_DOWN));

	if (state->pitch < -VIEW_MAX_PITCH) {
		state->pitch = -VIEW_MAX_PITCH;
	} else if (state->pitch > VIEW_MAX_PITCH) {
		state->pitch = VIEW_MAX_PITCH;
	}

	if (state->distance < VIEW_MIN_DISTANCE) {
		state->distance = VIEW_MIN_DISTANCE;
	} else if (state->distance > VIEW_MAX_DISTANCE) {
		state->distance = VIEW_MAX_DISTANCE;
	}
}

static enum mh_error_code
animate_mesh(struct mh_mesh *mesh, struct mh_renderer *renderer, double seconds,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_mesh_animate(mesh, seconds, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!mh_renderer_update_vertices(renderer))
		return mh_renderer_error(renderer, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_model_viewer_run(struct mh_mesh *mesh, const char *title,
	const struct mh_model_viewer_options *options)
{
	struct mh_error diagnostic;
	struct mh_window *window;
	struct mh_renderer *renderer;
	struct mh_event event;
	struct viewer_state state;
	float view_projection[16];
	float view[16];
	double elapsed_seconds;
	struct mh_clock_counter previous, now, frequency;
	unsigned long low, high;
	int success;
	int first_frame;
	struct mh_error error;
	enum mh_error_code status;

	if (options->headless) {
		mh_sdl_window_offscreen(0);
	}

	if (!mh_sdl_window_acquire()) {
		mh_platform_error(&diagnostic);
		fprintf(stderr, "SDL initialization: %s\n", diagnostic.message);
		return 0;
	}

	window = options->headless
		? NULL
		: mh_window_create(title, MH_VIEW_WIDTH, MH_VIEW_HEIGHT, 0);

	if (window == NULL && !options->headless) {
		mh_platform_error(&diagnostic);
		fprintf(stderr, "Window: %s\n", diagnostic.message);
		mh_platform_shutdown();
		return 0;
	}

	renderer = mh_renderer_create(window, mesh);

	if (renderer == NULL) {
		mh_platform_error(&diagnostic);
		fprintf(stderr, "Renderer: %s\n", diagnostic.message);
		mh_window_destroy(window);
		mh_platform_shutdown();
		return 0;
	}

	state.yaw = options->yaw;
	state.reflections = options->reflections;
	state.pitch = options->pitch;
	state.distance = VIEW_DEFAULT_DISTANCE;
	state.animation_seconds = options->animation_seconds;
	state.paused = options->paused;
	first_frame = 1;
	previous = mh_clock_counter();
	state.running = 1;
	success = 1;

	while (state.running) {
		state.time_changed = first_frame;

		while (mh_event_poll(NULL, &event))
			handle_event(&state, &event, options);

		now = mh_clock_counter();
		frequency = mh_clock_frequency();
		low = (now.low - previous.low) & MH_U32_MAX;
		high = (now.high - previous.high - (now.low < previous.low)) &
			MH_U32_MAX;
		elapsed_seconds = ((double)high * MH_U32_MODULUS + low) /
			((double)frequency.high * MH_U32_MODULUS +
				frequency.low);
		previous = now;

		if (!first_frame && !state.paused && !state.time_changed &&
			options->capture_path == NULL) {
			state.animation_seconds += elapsed_seconds;
			state.time_changed = 1;
		}

		first_frame = 0;

		if (state.time_changed && mesh->scenery != NULL) {
			status = animate_mesh(mesh, renderer,
				state.animation_seconds, &error);

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "Animation: %s\n",
					error.message);
				success = 0;
				break;
			}
		}

		update_orbit(&state, elapsed_seconds);

		orbit_matrices(view_projection, view, state.yaw, state.pitch,
			state.distance);

		if (!mh_renderer_frame(renderer, view_projection, view,
			state.reflections)) {
			mh_renderer_error(renderer, &diagnostic);
			fprintf(stderr, "Frame: %s\n", diagnostic.message);
			success = 0;
			break;
		}

		if (options->capture_path != NULL) {
			if (!mh_renderer_capture(
				renderer, options->capture_path)) {
				mh_renderer_error(renderer, &diagnostic);
				fprintf(stderr, "Capture: %s\n",
					diagnostic.message);
				success = 0;
			}

			break;
		}

		mh_clock_delay(VIEW_FRAME_DELAY_MS);
	}

	mh_renderer_destroy(renderer);
	mh_window_destroy(window);
	mh_platform_shutdown();

	return success;
}
