#include <emscripten/html5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_depth.h"
#include "backends/render/webgl_history.h"
#include "backends/render/webgl_presentation.h"

static unsigned int failures;

static void
check(int success, const char *label)
{
	if (success)
		printf("%s\n", label);
	else {
		fprintf(stderr, "%s failed\n", label);
		failures++;
	}
}

static int
operation(enum mh_error_code status, const struct mh_error *error)
{
	if (status == MH_ERROR_NONE)
		return 1;

	fprintf(stderr, "%s\n", error->message);
	failures++;

	return 0;
}

static int
pixel(const struct mh_webgl_target *target, int red, int green, int blue)
{
	struct mh_image image;
	struct mh_error error;
	size_t i;
	int success;

	if (!operation(mh_webgl_read_pixels(target, &image, &error), &error))
		return 0;

	success = 1;

	for (i = 0; i < (size_t)image.width * image.height; i++)
		if (abs(image.rgba[i * 4] - red) > 1 ||
			abs(image.rgba[i * 4 + 1] - green) > 1 ||
			abs(image.rgba[i * 4 + 2] - blue) > 1 ||
			image.rgba[i * 4 + 3] != 255) {
			fprintf(stderr,
				"pixel %lu: got %u,%u,%u,%u expected %d,%d,%d,255\n",
				(unsigned long)i, image.rgba[i * 4],
				image.rgba[i * 4 + 1], image.rgba[i * 4 + 2],
				image.rgba[i * 4 + 3], red, green, blue);
			success = 0;
			break;
		}

	free(image.rgba);

	return success;
}

static void
paint(int red)
{
	glClearColor(red / 255.0f, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
}

static const struct mh_webgl_target *
begin(struct mh_webgl_history *history, int scene, const float *fog)
{
	const struct mh_webgl_target *target;
	struct mh_error error;

	if (!operation(mh_webgl_history_begin(
			history, scene, fog, &target, &error),
		&error))
		return NULL;

	return target;
}

static void
finish(struct mh_webgl_history *history)
{
	struct mh_error error;

	operation(mh_webgl_history_finish(history, &error), &error);
}

static int
open_history(struct mh_webgl_history *history)
{
	struct mh_error error;

	memset(history, 0, sizeof(*history));

	return operation(
		mh_webgl_history_create(history, 7, 5, &error), &error);
}

static void
clear_colors(void)
{
	struct mh_webgl_history history;
	const struct mh_webgl_target *target;
	static const float fog[] = {.25f, .5f, .75f};
	GLint framebuffer, viewport[4];

	if (!open_history(&history))
		return;

	glEnable(GL_SCISSOR_TEST);
	glScissor(0, 0, 1, 1);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_FALSE);
	target = begin(&history, 1, NULL);

	if (target != NULL) {
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
		glGetIntegerv(GL_VIEWPORT, viewport);
		check((GLuint)framebuffer == target->framebuffer &&
				viewport[0] == 0 && viewport[1] == 0 &&
				viewport[2] == 7 && viewport[3] == 5 &&
				!glIsEnabled(GL_SCISSOR_TEST),
			"frame begin selects full viewport and target");
		check(pixel(target, 14, 18, 24),
			"unfogged scenes use the native background fallback");
		finish(&history);
	}

	target = begin(&history, 1, fog);

	if (target != NULL) {
		check(pixel(target, 64, 128, 191),
			"fog supplies scene clear color");
		finish(&history);
	}

	target = begin(&history, 0, fog);

	if (target != NULL) {
		check(pixel(target, 0, 0, 0),
			"UI and TRON frames clear to opaque black");
		finish(&history);
	}

	mh_webgl_history_destroy(&history);
}

static void
retention(unsigned int frames)
{
	struct mh_webgl_history history;
	const struct mh_webgl_target *target;
	struct mh_error error;
	unsigned int frame;
	int expected;

	if (!open_history(&history))
		return;

	if (!operation(mh_webgl_history_frames(&history, frames, &error),
		&error) ||
		!operation(
			mh_webgl_history_blur(&history, 1, &error), &error)) {
		mh_webgl_history_destroy(&history);
		return;
	}

	for (frame = 1; frame <= frames * 3; frame++) {
		target = begin(&history, 1, NULL);

		if (target == NULL)
			break;

		expected = frame > frames ? (int)(frame - frames) * 20 : 0;

		if (!pixel(target, expected, 0, 0)) {
			fprintf(stderr, "%u-frame history: frame %u\n", frames,
				frame);
			check(0, "history retained the wrong frame");
		}

		paint((int)frame * 20);
		finish(&history);
		check(pixel(&history.front.target, (int)frame * 20, 0, 0),
			"completed front target contains the latest full frame");

		/* Repeating this setting must preserve the retained frames. */
		operation(mh_webgl_history_frames(&history, frames, &error),
			&error);
	}

	printf("%u-frame history: initial black and retained swap order checked\n",
		frames);
	mh_webgl_history_destroy(&history);
}

static void
switching(void)
{
	struct mh_webgl_history history;
	const struct mh_webgl_target *target;
	struct mh_error error;
	unsigned int i;

	if (!open_history(&history))
		return;

	if (begin(&history, 1, NULL) != NULL) {
		paint(80);
		finish(&history);
	}

	operation(mh_webgl_history_blur(&history, 1, &error), &error);
	target = begin(&history, 1, NULL);

	if (target != NULL) {
		check(pixel(target, 0, 0, 0),
			"first blur enable starts its new back buffer black");
		paint(160);
		finish(&history);
	}

	target = begin(&history, 1, NULL);

	if (target != NULL) {
		check(pixel(target, 80, 0, 0),
			"first blur enable retains the existing front frame");
		paint(100);
		finish(&history);
	}

	operation(mh_webgl_history_blur(&history, 0, &error), &error);
	target = begin(&history, 1, NULL);

	if (target != NULL) {
		check(pixel(target, 14, 18, 24),
			"disabling blur clears color on each new scene");
		paint(200);
		finish(&history);
	}

	operation(mh_webgl_history_blur(&history, 1, &error), &error);
	target = begin(&history, 0, NULL);

	if (target != NULL) {
		check(pixel(target, 0, 0, 0),
			"UI-only frame ignores enabled blur history");
		paint(240);
		finish(&history);
	}

	target = begin(&history, 1, NULL);

	if (target != NULL) {
		check(pixel(target, 200, 0, 0),
			"history continues rotating while blur is disabled");
		finish(&history);
	}

	target = begin(&history, 1, NULL);

	if (target != NULL) {
		check(pixel(target, 240, 0, 0),
			"history retains complete frames including UI-only output");
		finish(&history);
	}

	operation(mh_webgl_history_frames(&history, 3, &error), &error);

	for (i = 0; i < 3; i++) {
		target = begin(&history, 1, NULL);

		if (target == NULL)
			break;

		check(pixel(target, 0, 0, 0),
			"switching to three frames resets history to black");
		paint(20);
		finish(&history);
	}

	operation(mh_webgl_history_frames(&history, 2, &error), &error);

	for (i = 0; i < 2; i++) {
		target = begin(&history, 1, NULL);

		if (target == NULL)
			break;

		check(pixel(target, 0, 0, 0),
			"switching to two frames resets history to black");
		paint(20);
		finish(&history);
	}

	mh_webgl_history_destroy(&history);
}

static void
depth_and_cancel(void)
{
	struct mh_webgl_history history;
	struct mh_webgl_depth depth;
	const struct mh_webgl_target *target, *unchanged;
	struct mh_depth_probe probe = {1, 1, 1, .75f};
	struct mh_error error;
	unsigned char visible;
	unsigned int i;
	GLuint front;

	memset(&depth, 0, sizeof(depth));

	if (!open_history(&history))
		return;

	operation(mh_webgl_history_blur(&history, 1, &error), &error);

	for (i = 0; i < 2; i++) {
		if (begin(&history, 1, NULL) == NULL)
			break;

		glClearDepthf(.25f);
		glClear(GL_DEPTH_BUFFER_BIT);
		paint(80);
		finish(&history);
	}

	target = begin(&history, 1, NULL);

	if (target != NULL) {
		check(pixel(target, 80, 0, 0),
			"retained color survives the next depth clear");
		visible = 0;
		check(mh_webgl_depth_queue(&depth, target, &probe, 1, NULL) ==
					MH_ERROR_NONE &&
				mh_webgl_depth_read(&depth, &visible, 1,
					NULL) == MH_ERROR_NONE &&
				visible == 1,
			"depth clears every frame even when color is retained");
		unchanged = NULL;
		check(mh_webgl_history_begin(&history, 1, NULL, &unchanged,
			NULL) == MH_ERROR_STATE &&
				unchanged == NULL &&
				mh_webgl_history_blur(&history, 0, NULL) ==
					MH_ERROR_STATE &&
				mh_webgl_history_frames(&history, 3, NULL) ==
					MH_ERROR_STATE,
			"an active frame cannot be replaced or reconfigured");
		front = history.front.target.framebuffer;
		paint(200);
		mh_webgl_history_cancel(&history);
		check(history.front.target.framebuffer == front &&
				pixel(&history.front.target, 80, 0, 0),
			"cancelled frame does not replace the completed front buffer");
	}

	target = begin(&history, 1, NULL);

	if (target != NULL) {
		check(pixel(target, 0, 0, 0),
			"cancelled frame is not reused as valid blur history");
		finish(&history);
	}

	check(mh_webgl_history_finish(&history, NULL) == MH_ERROR_STATE &&
			mh_webgl_history_blur(&history, 2, NULL) ==
				MH_ERROR_INVALID &&
			mh_webgl_history_frames(&history, 1, NULL) ==
				MH_ERROR_INVALID &&
			mh_webgl_history_create(&history, 7, 5, NULL) ==
				MH_ERROR_STATE,
		"invalid transitions and duplicate ownership are rejected");
	front = history.front.target.framebuffer;
	mh_webgl_history_destroy(&history);
	mh_webgl_history_destroy(&history);
	check(!glIsFramebuffer(front),
		"repeated history destruction releases frame targets");
	mh_webgl_depth_destroy(&depth);
}

int	mh_webgl_history_check(void);

int
mh_webgl_history_check(void)
{
	EmscriptenWebGLContextAttributes attributes;
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context;

	failures = 0;
	emscripten_webgl_init_context_attributes(&attributes);
	attributes.majorVersion = 2;
	attributes.antialias = EM_FALSE;
	context = emscripten_webgl_create_context("#canvas", &attributes);

	if (context <= 0 ||
		emscripten_webgl_make_context_current(context) !=
			EMSCRIPTEN_RESULT_SUCCESS)
		return 1;

	clear_colors();
	retention(2);
	retention(3);
	switching();
	depth_and_cancel();
	check(glGetError() == GL_NO_ERROR,
		"frame history leaves no WebGL errors");
	emscripten_webgl_destroy_context(context);

	return failures != 0;
}

int
main(void)
{
	return 0;
}
