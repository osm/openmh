#include <stddef.h>
#include <string.h>

#include "engine/core/clock.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/core/text.h"
#include "engine/game/mode.h"
#include "engine/ui/attract.h"
#include "engine/ui/hud.h"

#define COURSE_LIMIT 4096
#define ATTRACT_RACE_LAPS 25
#define DIVISION_BASE 100
#define SPECIAL_COURSE_SUFFIX 10
#define LAST_ATTRACT_DIVISION 3
#define LABEL_PERIOD_MS 500
#define LABEL_HALF_MS 250
#define LABEL_Y 4
#define SELECTION_ATTEMPTS 65536UL
#define RETURN_IDLE_MS 10000
#define MAIN_IDLE_MS 30000UL
#define OTHER_IDLE_MS 1000000000UL
#define CAMERA_CYCLE_MS 20000
#define CAMERA_HALF_MS 10000
#define RACE_DURATION_MS 40000.0
#define CLOCK_LIMIT 9223372036854775808.0L

static int
name_valid(const char *name)
{
	unsigned int i;

	if (name == NULL)
		return 0;

	for (i = 0; i < MH_ATTRACT_NAME_BYTES; i++)
		if (name[i] == 0)
			return 1;

	return 0;
}

static int
same_name(const char *a, const char *b)
{
	return mh_ascii_equal_n(a, b, MH_ATTRACT_NAME_BYTES);
}

static int
eligible_course(const struct mh_attract_course *course, const char *previous)
{
	return course->division % DIVISION_BASE < SPECIAL_COURSE_SUFFIX &&
		course->division / DIVISION_BASE <= LAST_ATTRACT_DIVISION &&
		!same_name(previous, course->name);
}

enum mh_error_code
mh_attract_course_select(const struct mh_attract_course *courses,
	unsigned int count, const char previous[MH_ATTRACT_NAME_BYTES],
	unsigned long *random, unsigned int *selected,
	struct mh_error *error_out)
{
	unsigned long seed, attempt;
	unsigned int i, available;

	if (courses == NULL || !name_valid(previous) || random == NULL ||
		selected == NULL || count == 0 || count > COURSE_LIMIT ||
		*random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid attract course catalog");

	available = 0;

	for (i = 0; i < count; i++) {
		if (courses[i].division > MH_U32_MAX ||
			!name_valid(courses[i].name))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid attract course");

		if (eligible_course(&courses[i], previous))
			available++;
	}

	if (available == 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"no alternate attract course");

	seed = *random;

	for (attempt = 0; attempt < SELECTION_ATTEMPTS; attempt++) {
		i = mh_random_next(&seed) % count;

		if (eligible_course(&courses[i], previous)) {
			*selected = i;
			*random = seed;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	return mh_error_set(error_out, MH_ERROR_FAILED,
		"attract course selection exhausted");
}

enum mh_error_code
mh_attract_settings(const struct mh_settings *saved, unsigned int track,
	struct mh_settings *runtime, struct mh_error *error_out)
{
	struct mh_settings next;

	if (saved == NULL || runtime == NULL || track >= COURSE_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid attract settings");

	next = *saved;
	next.game_type = MH_GAME_ATTRACT;
	next.track = track;
	next.laps = ATTRACT_RACE_LAPS;
	next.difficulty = 2;
	next.catch_up = 0;
	next.av.info_detail = next.av.checkpoint_info = next.av.road_map = 0;
	next.av.checkpoint_delay = 0;
	next.av.name_plates = 0;
	next.av.car_detail = next.av.car_shading = 2;
	next.av.lens_flares = next.av.sparks = next.av.smoke = next.av.halos =
		1;
	next.av.shadow = next.av.sky_image = next.av.track_detail = 1;
	*runtime = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

unsigned long
mh_attract_idle_delay(
	int main_page, unsigned int previous_mode, unsigned long exit_flag)
{
	if (previous_mode == MH_GAME_ATTRACT && exit_flag == 0)
		return RETURN_IDLE_MS;

	return main_page ? MAIN_IDLE_MS : OTHER_IDLE_MS;
}

enum mh_error_code
mh_attract_idle_poll(unsigned long *started, unsigned long delay,
	long double now_ms, long double stamp_ms, int *requested,
	struct mh_error *error_out)
{
	unsigned long stamp;
	enum mh_error_code status;
	int due;

	if (started == NULL || requested == NULL || *started > MH_U32_MAX ||
		delay > MH_U32_MAX || !(now_ms >= 0 && now_ms < CLOCK_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid attract idle clock");

	due = now_ms - (long double)*started > (long double)delay;

	if (due) {
		status = mh_clock_word(stamp_ms, &stamp, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		*started = stamp;
	}

	*requested = due;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_attract_frame(const struct mh_attract_view *state, unsigned int count,
	unsigned int active_mask, unsigned long go_ms, long double now_ms,
	struct mh_attract_frame *output, struct mh_error *error_out)
{
	struct mh_attract_frame next;
	unsigned long clock_ms, elapsed_ms;
	enum mh_error_code status;

	if (state == NULL || output == NULL || count == 0 ||
		count > MH_TRAFFIC_MAX_CARS || state->viewed >= count ||
		active_mask == 0 || active_mask >= (1U << count) ||
		go_ms > MH_U32_MAX || state->in_car > 1 ||
		state->requested_in_car > 1 || state->chase > 1 ||
		state->track > 1 || state->intro > 1 ||
		state->phase > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid attract view");

	status = mh_clock_word(now_ms, &clock_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));
	next.view = *state;
	next.label = clock_ms % LABEL_PERIOD_MS > LABEL_HALF_MS;

	if (go_ms != 0) {
		elapsed_ms = (clock_ms - go_ms) & MH_U32_MAX;
		next.view.in_car = next.view.requested_in_car = 1;
		next.view.chase = next.view.track = next.view.intro = 0;

		if (elapsed_ms % CAMERA_CYCLE_MS < CAMERA_HALF_MS) {
			if (next.view.phase == 0)
				next.view.phase = 1;
		} else {
			next.view.in_car = next.view.requested_in_car = 0;
			next.view.track = 1;

			if (state->phase != 0) {
				next.release_mask = 1U << state->viewed;

				do {
					next.view.viewed =
						(next.view.viewed + 1) % count;
				} while (!(active_mask &
					(1U << next.view.viewed)));

				next.view.phase = 0;
			}
		}

		next.finished = now_ms - (long double)go_ms > RACE_DURATION_MS;
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_attract_field_frame(struct mh_field *field,
	const struct mh_drive_frame *sample, unsigned long *phase,
	struct mh_attract_frame *output, struct mh_error *error_out)
{
	struct mh_attract_view view;
	struct mh_attract_frame next;
	struct mh_drive *drive;
	unsigned int listener_mask;
	unsigned long listener_revision;
	enum mh_error_code status;

	if (field == NULL || sample == NULL || phase == NULL ||
		output == NULL || !field->attract || field->count < 1 ||
		field->count > MH_TRAFFIC_MAX_CARS ||
		field->viewed_entry >= field->count)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid attract field");

	drive = &field->cars[field->viewed_entry];

	if (drive->camera_mode != MH_CAMERA_CHASE &&
		drive->camera_mode != MH_CAMERA_IN_CAR &&
		drive->camera_mode != MH_CAMERA_TRACK)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid attract camera");

	memset(&view, 0, sizeof(view));
	view.viewed = field->viewed_entry;
	view.phase = *phase;
	view.in_car = mh_drive_camera_in_car(drive);
	view.requested_in_car = drive->camera_mode == MH_CAMERA_IN_CAR;
	view.chase = drive->camera_mode == MH_CAMERA_CHASE;
	view.track = drive->camera_mode == MH_CAMERA_TRACK;
	view.intro = drive->camera_intro != 0;
	status = mh_attract_frame(&view, field->count, field->active_mask,
		sample->time.go_ms, (long double)sample->seconds * 1000.0L,
		&next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	listener_mask = field->listener_mask;
	listener_revision = field->listener_revision;

	if (next.view.viewed != field->viewed_entry) {
		status = mh_field_select_view(
			field, next.view.viewed, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	drive = &field->cars[field->viewed_entry];

	if (sample->time.go_ms != 0) {
		drive->camera_mode =
			next.view.track ? MH_CAMERA_TRACK : MH_CAMERA_IN_CAR;
		drive->camera_intro = 0;
	}

	/*
	 * Ordinary view selection also binds its new detailed listener. Mode 5
	 * deliberately only releases the old one, even when in-car resumes.
	 */
	field->listener_mask = listener_mask & ~next.release_mask;
	field->listener_revision = listener_revision + (next.release_mask != 0);
	*phase = next.view.phase;
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_attract_label_draw(const struct mh_hud_assets *assets, int visible,
	struct mh_ui_list *list)
{
	static const unsigned char label[] = "DEMO";
	static const float colors[2][4] = {{0, 0, 0, 1}, {1, 1, 1, 1}};
	unsigned int i, pass, width;
	int x, offset;

	if (assets == NULL || list == NULL || (visible != 0 && visible != 1))
		return 0;

	if (!visible)
		return 1;

	width = 0;

	for (i = 0; i < sizeof(label) - 1; i++) {
		if (assets->hardware_advance[label[i]] > MH_HUD_FONT_LIMIT)
			return 0;

		width += assets->hardware_advance[label[i]];
	}

	for (pass = 0; pass < 2; pass++) {
		offset = pass == 0 ? 1 : 0;
		x = (int)(list->width / 2) - (int)(width / 2) + offset;

		for (i = 0; i < sizeof(label) - 1; i++) {
			if (!mh_ui_quad(list, &assets->atlas,
				&assets->hardware_glyphs[label[i]],
				(float)x, (float)(LABEL_Y + offset),
				colors[pass]))
				return 0;

			x += (int)assets->hardware_advance[label[i]];
		}
	}

	return 1;
}
