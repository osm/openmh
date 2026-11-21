#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/pixel_snap.h"
#include "engine/ui/tron_intro.h"

#define VIEWPORT_LIMIT 16384
#define ATTRIBUTE_LIMIT 1e9f
#define INTRO_WIDTH 512.0f
#define INTRO_HEIGHT 384.0f

enum intro_time {
	BACKDROP_START = 3,
	TEXT_START = 4,
	TEXT_END = 52,
	BACKDROP_END = 53
};

const char *
mh_tron_intro_text(unsigned int line)
{
	/*
	 * Fifteen slots; leading/trailing empty lines control the timing and
	 * are not collapsed.
	 */
	static const char *const text[MH_TRON_INTRO_LINES] = {"", "", "", "",
		"", "Hello there",
		"Here is something for all the oldskool people out there:",
		"A tribute to the classic movie TRON!",
		"Enjoy the look and feel of the very early 80's --",
		"when hidden-lines were 'in'", "and never forget your roots!",
		"", "Regards,",
		"all the former demosceners at Digital Illusions", ""};

	return line < MH_TRON_INTRO_LINES ? text[line] : NULL;
}

static void
prepare_text_lines(float time, struct mh_tron_intro *frame)
{
	long double step;
	float scroll_offset;
	unsigned int first, i;

	step = ((long double)time - TEXT_START) * .25f;
	first = (unsigned int)step;
	scroll_offset = (float)(1 - (step - first) * 5);

	if (scroll_offset < 0)
		scroll_offset = 0;

	for (i = 0; i < MH_TRON_INTRO_VISIBLE_LINES &&
		first + i < MH_TRON_INTRO_LINES;
		i++) {
		frame->lines[i].line = first + i;
		frame->lines[i].x = 25;
		frame->lines[i].y =
			(int)((i * (long double)(double).05 + (double).77 +
				(long double)scroll_offset *
					(double).05) *
				(double)INTRO_HEIGHT);
		frame->count++;
	}
}

enum mh_error_code
mh_tron_intro_frame(unsigned int width, unsigned int height, float time,
	float dt, int active, struct mh_tron_intro *output,
	struct mh_error *error_out)
{
	struct mh_tron_intro frame;

	if (width < 1 || width > VIEWPORT_LIMIT || height < 1 ||
		height > VIEWPORT_LIMIT || output == NULL ||
		!(time >= 0 && time <= ATTRIBUTE_LIMIT) ||
		!(dt >= 0 && dt <= 60) || (active != 0 && active != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid TRON introduction");

	memset(&frame, 0, sizeof(frame));
	frame.next_time = time;

	if (active) {
		frame.next_time = (float)((long double)time + dt);

		if (time > BACKDROP_START && time < BACKDROP_END) {
			frame.backdrop = 1;
			frame.band_top =
				(float)((long double)height * (double).88);
			frame.band_bottom =
				(float)((long double)height * (double).9);

			if (time < TEXT_START)
				frame.opacity =
					(float)(((long double)time -
							BACKDROP_START) *
						255);
			else if (time > TEXT_END)
				frame.opacity =
					(float)((BACKDROP_END -
							(long double)time) *
						255);
			else
				frame.opacity = 255;
		}

		if (time > TEXT_START && time < TEXT_END)
			prepare_text_lines(time, &frame);
	}

	*output = frame;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_tron_intro_corner(unsigned int width, unsigned int height, float x, float y,
	float xy[2], float *alpha, struct mh_error *error_out)
{
	long double relative_y;
	float opacity, pixel_x, pixel_y;

	if (width < 1 || width > VIEWPORT_LIMIT || height < 1 ||
		height > VIEWPORT_LIMIT || xy == NULL || alpha == NULL ||
		!(x >= -ATTRIBUTE_LIMIT && x <= ATTRIBUTE_LIMIT) ||
		!(y >= -ATTRIBUTE_LIMIT && y <= ATTRIBUTE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid TRON introduction corner");

	relative_y = (long double)y * (float)(1.0f / INTRO_HEIGHT);
	pixel_x = mh_pixel_snap(
		(long double)x * (float)(1.0f / INTRO_WIDTH) * width);
	pixel_y = mh_pixel_snap(relative_y * height);
	opacity =
		(float)((relative_y + (double)-.9) * (double)50 * (double)255);

	if (opacity < 0)
		opacity = 0;

	if (opacity > 255)
		opacity = 255;

	xy[0] = pixel_x;
	xy[1] = pixel_y;
	*alpha = opacity;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
