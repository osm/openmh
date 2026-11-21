#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/render/viewport.h"
#include "engine/settings/av.h"
#include "engine/ui/loading_art.h"
#include "engine/ui/loading_screen.h"

#define LABEL_X 370
#define VALUE_X 500
#define COLUMN_OFFSET -60
#define VALUE_OFFSET 20

void
mh_loading_progress_init(struct mh_loading_progress *progress)
{
	progress->remaining = MH_LOADING_STAGES;
	progress->marked = 0;
}

enum mh_error_code
mh_loading_progress_step(struct mh_loading_progress *progress,
	unsigned long milliseconds, struct mh_error *error_out)
{
	if (progress == NULL || progress->remaining > MH_LOADING_STAGES ||
		milliseconds > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loading progress");

	if (progress->remaining != 0)
		progress->remaining--;

	if (milliseconds != 0)
		progress->marked |= 1U << progress->remaining;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
place_rows(unsigned int width, unsigned int height,
	enum mh_render_profile profile, struct mh_loading_layout *layout)
{
	static const unsigned int row_y[MH_LOADING_ROWS] = {10, 30, 40, 60, 70};
	float scale_x, scale_y;
	long double label_x, value_x, y;
	unsigned int row_index;

	scale_x = (float)((long double)width / MH_LOADING_WIDTH);
	scale_y = (float)((long double)height / MH_LOADING_HEIGHT);
	label_x = (long double)scale_x * LABEL_X;
	value_x = (long double)scale_x * VALUE_X;

	/* Hardware caches these products in binary32 before adding offsets. */
	if (profile != MH_RENDER_SOFTWARE) {
		label_x = (float)label_x;
		value_x = (float)value_x;
	}

	label_x += COLUMN_OFFSET;
	value_x += COLUMN_OFFSET;
	value_x += VALUE_OFFSET;

	for (row_index = 0; row_index < MH_LOADING_ROWS; row_index++) {
		y = (long double)scale_y * row_y[row_index];

		if (profile != MH_RENDER_SOFTWARE || row_index == 1)
			y = (float)y;

		/* Hardware also caches the first two rows' column sums. */
		if (profile != MH_RENDER_SOFTWARE && row_index < 2) {
			layout->rows[row_index].label_x = (int)(float)label_x;
			layout->rows[row_index].value_x = (int)(float)value_x;
		} else {
			layout->rows[row_index].label_x = (int)label_x;
			layout->rows[row_index].value_x = (int)value_x;
		}

		layout->rows[row_index].y = (int)y;
	}
}

static void
format_record_time(unsigned long milliseconds, char text[MH_TRACK_TEXT_SIZE])
{
	sprintf(text, "%lu:%02lu:%02lu", milliseconds / 60000,
		(milliseconds / 1000) % 60, (milliseconds / 10) % 100);
}

enum mh_error_code
mh_loading_layout(const struct mh_track *track, const char *name,
	unsigned int system, unsigned long lap_ms, unsigned long race_ms,
	unsigned int width, unsigned int height, enum mh_render_profile profile,
	struct mh_loading_layout *output, struct mh_error *error_out)
{
	static const char *const labels[MH_LOADING_ROWS] = {"Name:", "Length:",
		"Location:", "Best laptime:", "Best racetime:"};

	struct mh_loading_layout next;
	unsigned int name_length, row_index;

	if (track == NULL || name == NULL || output == NULL ||
		system > MH_UNITS_IMPERIAL || lap_ms > MH_U32_MAX ||
		race_ms > MH_U32_MAX || width < MH_VIEW_MIN_WIDTH ||
		width > MH_VIEW_MAX_WIDTH || height < MH_VIEW_MIN_HEIGHT ||
		height > MH_VIEW_MAX_HEIGHT ||
		(unsigned int)profile >= MH_RENDER_PROFILE_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loading-screen information");

	for (name_length = 0;
		name_length < MH_TRACK_TEXT_SIZE && name[name_length] != 0;
		name_length++)
		;

	if (name_length == MH_TRACK_TEXT_SIZE ||
		memchr(track->location, 0, sizeof(track->location)) == NULL ||
		memchr(track->length, 0, sizeof(track->length)) == NULL ||
		memchr(track->length_us, 0, sizeof(track->length_us)) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated loading-screen text");

	memset(&next, 0, sizeof(next));
	place_rows(width, height, profile, &next);

	for (row_index = 0; row_index < MH_LOADING_ROWS; row_index++)
		next.rows[row_index].label = labels[row_index];

	strcpy(next.rows[0].value, name);
	strcpy(next.rows[1].value,
		system == MH_UNITS_METRIC ? track->length : track->length_us);
	strcpy(next.rows[2].value, track->location);
	format_record_time(lap_ms, next.rows[3].value);
	format_record_time(race_ms, next.rows[4].value);
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
