#include <float.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_graphics.h"
#include "engine/ui/menu_graphics_fps.h"
#include "engine/ui/menu_slider.h"

#define CHECKPOINT_DELAY_LIMIT 10000
#define VIEW_DISTANCE_MIN 30
#define VIEW_DISTANCE_MAX 150
#define MODE_INITIAL_SCORE 100000000L
#define MODE_FALLBACK_SCORE 50000000L

static int
valid_settings(const struct mh_av_settings *settings)
{
	unsigned int i;

	if (settings == NULL || settings->info_detail > 2 ||
		settings->road_map > 2 || settings->checkpoint_info > 2 ||
		settings->checkpoint_delay > CHECKPOINT_DELAY_LIMIT ||
		settings->detail_mode > MH_DETAIL_CUSTOM ||
		settings->lens_flares > 1 || settings->sparks > 1 ||
		settings->smoke > 1 || settings->halos > 1 ||
		settings->shadow > 1 || settings->skid_marks > 1 ||
		settings->name_plates > 2 || settings->sky_image > 1 ||
		settings->track_detail > 1 || settings->car_detail > 2 ||
		settings->car_shading > MH_AV_SHADING_GLENZ ||
		settings->texture_precision > MH_AV_TEXTURE_32BIT ||
		settings->texture_filter > MH_AV_FILTER_TRILINEAR ||
		settings->frame_queue > MH_AV_FRAME_QUEUE_THREE ||
		settings->perspective_on > 1 || settings->motion_blur > 1 ||
		settings->vsync > 1 || settings->show_fps > 1 ||
		settings->max_fps > MH_AV_MAX_FPS ||
		settings->screenshot_format >= MH_SCREENSHOT_FORMAT_COUNT ||
		settings->fullscreen > 1 || settings->triple_buffer > 1 ||
		!(settings->view_distance >= VIEW_DISTANCE_MIN &&
			settings->view_distance <= VIEW_DISTANCE_MAX))
		return 0;

	for (i = 0; i < 3; i++)
		if (!(settings->brightness[i] >= -FLT_MAX &&
			settings->brightness[i] <= FLT_MAX))
			return 0;

	return 1;
}

static void
append_event(
	struct mh_menu_output *result, unsigned int kind, unsigned int value)
{
	result->events[result->count].kind = kind;

	result->events[result->count++].value = value;
}

static void
step_choice(unsigned int *value, unsigned int count, int right,
	struct mh_menu_output *result)
{
	unsigned int sound;

	sound = MH_MENU_SOUND_VALUE_LIMIT;

	if (right && *value < count - 1) {
		(*value)++;
		sound = MH_MENU_SOUND_VALUE_NEXT;
	}

	if (!right && *value != 0) {
		(*value)--;
		sound = MH_MENU_SOUND_VALUE_PREVIOUS;
	}

	append_event(result, MH_MENU_SOUND, sound);
}

static unsigned int
match_mode(const struct mh_menu_graphics_mode *old,
	const struct mh_menu_graphics_device *device)
{
	unsigned int i, selected;
	unsigned long dx, dy, product;
	long score, best;

	selected = 0;
	best = MODE_INITIAL_SCORE;

	for (i = 0; i < device->count; i++) {
		dx = (device->modes[i].width - old->width) & MH_U32_MAX;
		dy = (device->modes[i].height - old->height) & MH_U32_MAX;
		product = (dx * dy) & MH_U32_MAX;

		if (product >= MH_U32_SIGN_BIT)
			product = (0UL - product) & MH_U32_MAX;

		/*
		 * The absolute value of INT32_MIN wraps to INT32_MIN in the
		 * mode score, so it sorts ahead of every other distance.
		 */
		score = product >= MH_U32_SIGN_BIT
			? -1L - (long)(MH_U32_MAX - product)
			: (long)product;

		if (score < best) {
			best = score;
			selected = i;
		}
	}

	return best > MODE_FALLBACK_SCORE ? 0 : selected;
}

static void
adjust_slider(unsigned int selected, struct mh_av_settings *settings,
	float step, int right, struct mh_menu_output *result)
{
	long double average, value;
	volatile double stored;
	volatile float rounded;
	unsigned int sound;

	sound = MH_MENU_SOUND_SLIDER_LIMIT;

	if (selected == MH_MENU_GRAPHICS_BRIGHTNESS) {
		average = ((long double)settings->brightness[0] +
				settings->brightness[1] +
				settings->brightness[2]) *
			(long double)(1.0 / 3.0);
		stored = (double)average;

		/*
		 * Right compares the extended average; Left compares its stored
		 * double. Both reload the double for the actual adjustment.
		 */
		if ((right && average < 2) || (!right && stored > 0)) {
			value = (long double)step * (long double)0.005;
			value = right ? (long double)stored + value
				: (long double)stored - value;
			rounded = (float)value;

			if (right && value > 2)
				rounded = 2;

			if (!right && value < 0)
				rounded = 0;

			settings->brightness[0] = settings->brightness[1] =
				settings->brightness[2] = rounded;
			sound = right ? MH_MENU_SOUND_SLIDER_INCREASE
				: MH_MENU_SOUND_SLIDER_DECREASE;
		}
	} else if (selected == MH_MENU_GRAPHICS_CHECKPOINT_DELAY) {
		if ((right &&
			settings->checkpoint_delay <
				CHECKPOINT_DELAY_LIMIT) ||
			(!right && settings->checkpoint_delay != 0)) {
			value = (long double)step * 50;
			value = right ? settings->checkpoint_delay + value
				: settings->checkpoint_delay - value;
			rounded = (float)value;

			if (right && value > CHECKPOINT_DELAY_LIMIT)
				rounded = CHECKPOINT_DELAY_LIMIT;

			if (!right && rounded < 0)
				rounded = 0;

			settings->checkpoint_delay = (unsigned long)rounded;
			sound = right ? MH_MENU_SOUND_SLIDER_INCREASE
				: MH_MENU_SOUND_SLIDER_DECREASE;
		}
	} else if (selected == MH_MENU_GRAPHICS_VIEW_DISTANCE) {
		if ((right && settings->view_distance < VIEW_DISTANCE_MAX) ||
			(!right &&
				settings->view_distance > VIEW_DISTANCE_MIN)) {
			value = (long double)step * .5L;
			value = right ? settings->view_distance + value
				: settings->view_distance - value;
			rounded = (float)value;

			if (right && value > VIEW_DISTANCE_MAX)
				rounded = VIEW_DISTANCE_MAX;

			if (!right && value < VIEW_DISTANCE_MIN)
				rounded = VIEW_DISTANCE_MIN;

			settings->view_distance = rounded;
			sound = right ? MH_MENU_SOUND_SLIDER_INCREASE
				: MH_MENU_SOUND_SLIDER_DECREASE;
		}
	} else
		return;

	append_event(result, MH_MENU_SOUND, sound);
}

static unsigned int *
setting_value(unsigned int selected, struct mh_av_settings *settings,
	unsigned int *count)
{
	*count = 2;

	switch (selected) {
	case MH_MENU_GRAPHICS_SHOW_FPS:
		return &settings->show_fps;

	case MH_MENU_GRAPHICS_SCREENSHOT_FORMAT:
		*count = MH_SCREENSHOT_FORMAT_COUNT;
		return &settings->screenshot_format;

	case MH_MENU_GRAPHICS_VSYNC:
		return &settings->vsync;

	case MH_MENU_GRAPHICS_FULLSCREEN:
		return &settings->fullscreen;

	case MH_MENU_GRAPHICS_INFO_DETAIL:
		*count = 3;
		return &settings->info_detail;

	case MH_MENU_GRAPHICS_ROAD_MAP:
		*count = 3;
		return &settings->road_map;

	case MH_MENU_GRAPHICS_CHECKPOINT_INFO:
		*count = 3;
		return &settings->checkpoint_info;

	case MH_MENU_GRAPHICS_DETAIL:
		*count = 5;
		return &settings->detail_mode;

	case MH_MENU_GRAPHICS_LENS_FLARES:
		return &settings->lens_flares;
	case MH_MENU_GRAPHICS_SPARKS:
		return &settings->sparks;
	case MH_MENU_GRAPHICS_SMOKE:
		return &settings->smoke;
	case MH_MENU_GRAPHICS_HALOS:
		return &settings->halos;
	case MH_MENU_GRAPHICS_SHADOW:
		return &settings->shadow;
	case MH_MENU_GRAPHICS_SKID_MARKS:
		return &settings->skid_marks;
	case MH_MENU_GRAPHICS_NAME_PLATES:
		*count = 3;
		return &settings->name_plates;

	case MH_MENU_GRAPHICS_SKY:
		return &settings->sky_image;
	case MH_MENU_GRAPHICS_TRACK_DETAIL:
		return &settings->track_detail;
	case MH_MENU_GRAPHICS_CAR_DETAIL:
		*count = 3;
		return &settings->car_detail;

	case MH_MENU_GRAPHICS_CAR_SHADING:
		*count = 3;
		return &settings->car_shading;

	case MH_MENU_GRAPHICS_PERSPECTIVE:
		return &settings->perspective_on;
	case MH_MENU_GRAPHICS_MOTION_BLUR:
		return &settings->motion_blur;
	case MH_MENU_GRAPHICS_TEXTURE_PRECISION:
		return &settings->texture_precision;
	case MH_MENU_GRAPHICS_TRIPLE_BUFFER:
		return &settings->triple_buffer;
	case MH_MENU_GRAPHICS_FRAME_QUEUE:
		*count = 3;
		return &settings->frame_queue;

	case MH_MENU_GRAPHICS_TEXTURE_FILTER:
		*count = 4;
		return &settings->texture_filter;

	default:
		return NULL;
	}
}

static void
step_setting(struct mh_menu_graphics_state *state,
	struct mh_av_settings *settings,
	const struct mh_menu_graphics_catalog *catalog, int right,
	struct mh_menu_output *result)
{
	const struct mh_menu_graphics_mode *old;
	unsigned int *value, count, index;

	if (state->selected == MH_MENU_GRAPHICS_DEVICE) {
		old = &catalog->devices[state->device].modes[state->mode];

		index = state->device;

		while ((right && index + 1 < catalog->count) ||
			(!right && index != 0)) {
			index = right ? index + 1 : index - 1;

			if (catalog->devices[index].count != 0)
				break;
		}

		if (catalog->devices[index].count == 0)
			index = state->device;

		append_event(result, MH_MENU_SOUND,
			index == state->device ? MH_MENU_SOUND_VALUE_LIMIT
				: right	       ? MH_MENU_SOUND_VALUE_NEXT
					: MH_MENU_SOUND_VALUE_PREVIOUS);
		state->device = index;
		state->mode = match_mode(old, &catalog->devices[state->device]);

		if (!catalog->devices[state->device].glenz &&
			settings->car_shading == MH_AV_SHADING_GLENZ)
			settings->car_shading = MH_AV_SHADING_REFLECTION;

		return;
	}

	if (state->selected == MH_MENU_GRAPHICS_MODE) {
		step_choice(&state->mode, catalog->devices[state->device].count,
			right, result);
		return;
	}

	if (state->selected == MH_MENU_GRAPHICS_PERSPECTIVE_QUALITY) {
		index = settings->perspective == 16 ? 1 : 0;

		if (!right) {
			if (settings->perspective == 8)
				index = 2;

			if (settings->perspective == 4)
				index = 3;
		}

		step_choice(&index, 2, right, result);
		settings->perspective = 32U >> index;

		return;
	}

	if (state->selected == MH_MENU_GRAPHICS_SCREENSHOT_FORMAT) {
		if (mh_screenshot_format_available(
			1 - settings->screenshot_format))
			step_choice(&settings->screenshot_format,
				MH_SCREENSHOT_FORMAT_COUNT, right, result);

		return;
	}

	value = setting_value(state->selected, settings, &count);

	if (state->selected == MH_MENU_GRAPHICS_CAR_SHADING &&
		catalog->devices[state->device].glenz)
		count = MH_AV_SHADING_GLENZ + 1;

	if (value != NULL)
		step_choice(value, count, right, result);
}

enum mh_error_code
mh_menu_graphics_validate(const struct mh_menu_graphics_state *state,
	const struct mh_av_settings *settings,
	const struct mh_menu_graphics_catalog *catalog,
	struct mh_error *error_out)
{
	const struct mh_menu_graphics_device *device;
	unsigned int i, j;

	if (state == NULL || !mh_menu_graphics_fps_valid(state) ||
		!valid_settings(settings) || catalog == NULL ||
		catalog->devices == NULL || catalog->count == 0 ||
		catalog->count > MH_MENU_GRAPHICS_DEVICES ||
		state->selected >= MH_MENU_GRAPHICS_ROWS ||
		(catalog->fullscreen != 0 && catalog->fullscreen != 1) ||
		(catalog->presentation != 0 && catalog->presentation != 1) ||
		(catalog->screenshots != 0 && catalog->screenshots != 1) ||
		state->device >= catalog->count)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Graphics input");

	for (i = 0; i < catalog->count; i++) {
		device = &catalog->devices[i];

		if (device->modes == NULL ||
			device->count > MH_MENU_GRAPHICS_MODES ||
			(device->glenz != 0 && device->glenz != 1) ||
			(device->motion_blur != 0 &&
				device->motion_blur != 1) ||
			(device->texture_precision != 0 &&
				device->texture_precision != 1) ||
			(device->blur_history != 0 &&
				device->blur_history != 1) ||
			(device->frame_queue != 0 &&
				device->frame_queue != 1) ||
			(device->texture_filter != 0 &&
				device->texture_filter != 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid Graphics mode catalog");

		for (j = 0; j < device->count; j++)
			if (device->modes[j].width > MH_U32_MAX ||
				device->modes[j].height > MH_U32_MAX)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid Graphics mode dimensions");
	}

	if (state->selected == MH_MENU_GRAPHICS_FULLSCREEN &&
		!catalog->fullscreen)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"fullscreen requires a compatible window system");

	if ((state->selected == MH_MENU_GRAPHICS_VSYNC ||
		state->selected == MH_MENU_GRAPHICS_MAX_FPS) &&
		!catalog->presentation)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"race presentation controls are unavailable");

	if (state->selected == MH_MENU_GRAPHICS_SCREENSHOT_FORMAT &&
		!catalog->screenshots)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"screenshots are unavailable");

	if (state->mode >= catalog->devices[state->device].count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Graphics mode selection");

	if (state->selected == MH_MENU_GRAPHICS_MOTION_BLUR &&
		!catalog->devices[state->device].motion_blur)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"motion blur requires a compatible renderer");

	if (state->selected == MH_MENU_GRAPHICS_TRIPLE_BUFFER &&
		!catalog->devices[state->device].blur_history)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"blur history control requires a compatible renderer");

	if (state->selected == MH_MENU_GRAPHICS_FRAME_QUEUE &&
		!catalog->devices[state->device].frame_queue)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"frame queue control requires a compatible renderer");

	if (state->selected == MH_MENU_GRAPHICS_TEXTURE_FILTER &&
		!catalog->devices[state->device].texture_filter)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"texture filtering requires a compatible renderer");

	if (state->selected == MH_MENU_GRAPHICS_TEXTURE_PRECISION &&
		!catalog->devices[state->device].texture_precision)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"texture precision requires a compatible renderer");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

unsigned int
mh_menu_graphics_row_at(
	const struct mh_menu_graphics_catalog *catalog, unsigned int position)
{
	if (position <= MH_MENU_GRAPHICS_MODE)
		return position;

	if (catalog->fullscreen) {
		if (position == MH_MENU_GRAPHICS_MODE + 1)
			return MH_MENU_GRAPHICS_FULLSCREEN;

		position--;
	}

	if (catalog->presentation) {
		if (position == MH_MENU_GRAPHICS_MODE + 1)
			return MH_MENU_GRAPHICS_VSYNC;

		if (position == MH_MENU_GRAPHICS_MODE + 2)
			return MH_MENU_GRAPHICS_MAX_FPS;

		position -= 2;
	}

	if (position == MH_MENU_GRAPHICS_MODE + 1)
		return MH_MENU_GRAPHICS_SHOW_FPS;

	if (catalog->screenshots && position == MH_MENU_GRAPHICS_MODE + 2)
		return MH_MENU_GRAPHICS_SCREENSHOT_FORMAT;

	return position - 1 - catalog->screenshots;
}

unsigned int
mh_menu_graphics_row_position(
	const struct mh_menu_graphics_catalog *catalog, unsigned int row)
{
	if (row <= MH_MENU_GRAPHICS_MODE)
		return row;

	if (row == MH_MENU_GRAPHICS_FULLSCREEN)
		return MH_MENU_GRAPHICS_MODE + 1;

	if (row == MH_MENU_GRAPHICS_VSYNC)
		return MH_MENU_GRAPHICS_MODE + 1 + catalog->fullscreen;

	if (row == MH_MENU_GRAPHICS_MAX_FPS)
		return MH_MENU_GRAPHICS_MODE + 2 + catalog->fullscreen;

	if (row == MH_MENU_GRAPHICS_SHOW_FPS)
		return MH_MENU_GRAPHICS_MODE + 1 + catalog->fullscreen +
			2 * catalog->presentation;

	if (row == MH_MENU_GRAPHICS_SCREENSHOT_FORMAT)
		return MH_MENU_GRAPHICS_MODE + 2 + catalog->fullscreen +
			2 * catalog->presentation;

	return row + catalog->fullscreen + 2 * catalog->presentation + 1 +
		catalog->screenshots;
}

int
mh_menu_graphics_row_available(
	const struct mh_menu_graphics_device *device, unsigned int row)
{
	if (row == MH_MENU_GRAPHICS_MOTION_BLUR)
		return device->motion_blur;

	if (row == MH_MENU_GRAPHICS_TEXTURE_PRECISION)
		return device->texture_precision;

	if (row == MH_MENU_GRAPHICS_TEXTURE_FILTER)
		return device->texture_filter;

	if (row == MH_MENU_GRAPHICS_TRIPLE_BUFFER)
		return device->blur_history;

	if (row == MH_MENU_GRAPHICS_FRAME_QUEUE)
		return device->frame_queue;

	return 1;
}

enum mh_error_code
mh_menu_graphics_update(struct mh_menu_graphics_state *state,
	struct mh_av_settings *settings,
	const struct mh_menu_graphics_catalog *catalog,
	const struct mh_menu_graphics_input *input,
	struct mh_menu_output *output, struct mh_error *error_out)
{
	struct mh_menu_graphics_state next;
	const struct mh_menu_graphics_device *device;
	struct mh_av_settings values;
	struct mh_menu_output result;
	enum mh_error_code status;
	unsigned int sound, last, position;
	float step;

	if (input == NULL || output == NULL || input->elapsed_ms > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Graphics input");

	status = mh_menu_graphics_validate(state, settings, catalog, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *state;
	device = &catalog->devices[next.device];
	values = *settings;
	memset(&result, 0, sizeof(result));

	if (mh_menu_graphics_fps_update(&next, &values, input, &result))
		goto publish;

	step = mh_menu_slider_step(
		input->elapsed_ms, input->shift, input->control, input->alt);

	if (input->escape == 1) {
		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_CANCEL);
		append_event(&result, MH_MENU_PAGE, MH_MENU_OPTIONS);
	}

	if (input->up == 1) {
		sound = MH_MENU_SOUND_ROW_LIMIT;

		if (next.selected != MH_MENU_GRAPHICS_DEVICE) {
			position = mh_menu_graphics_row_position(
				catalog, next.selected);

			do {
				next.selected = mh_menu_graphics_row_at(
					catalog, --position);
			} while (!mh_menu_graphics_row_available(
				device, next.selected));

			sound = MH_MENU_SOUND_ROW_PREVIOUS;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->down == 1) {
		last = values.detail_mode == MH_DETAIL_CUSTOM
			? (device->motion_blur ? MH_MENU_GRAPHICS_MOTION_BLUR
					: MH_MENU_GRAPHICS_VIEW_DISTANCE)
			: MH_MENU_GRAPHICS_DETAIL;

		if (values.detail_mode == MH_DETAIL_CUSTOM &&
			device->texture_precision)
			last = MH_MENU_GRAPHICS_TEXTURE_PRECISION;

		if (values.detail_mode == MH_DETAIL_CUSTOM &&
			device->texture_filter)
			last = MH_MENU_GRAPHICS_TEXTURE_FILTER;

		if (values.detail_mode == MH_DETAIL_CUSTOM &&
			device->frame_queue)
			last = MH_MENU_GRAPHICS_FRAME_QUEUE;

		if (values.detail_mode == MH_DETAIL_CUSTOM &&
			device->blur_history)
			last = MH_MENU_GRAPHICS_TRIPLE_BUFFER;

		sound = MH_MENU_SOUND_ROW_LIMIT;

		position =
			mh_menu_graphics_row_position(catalog, next.selected);

		if (position < mh_menu_graphics_row_position(catalog, last)) {
			do {
				next.selected = mh_menu_graphics_row_at(
					catalog, ++position);
			} while (!mh_menu_graphics_row_available(
				device, next.selected));

			sound = MH_MENU_SOUND_ROW_NEXT;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->held_right == 1)
		adjust_slider(next.selected, &values, step, 1, &result);

	if (input->held_left == 1)
		adjust_slider(next.selected, &values, step, 0, &result);

	if (input->right == 1)
		step_setting(&next, &values, catalog, 1, &result);

	if (input->left == 1)
		step_setting(&next, &values, catalog, 0, &result);

publish:
	*state = next;
	*settings = values;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
