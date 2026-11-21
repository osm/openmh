#include <emscripten/html5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_depth.h"

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

static void
clear_depth(const struct mh_webgl_target *target, float value)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->framebuffer);
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_TRUE);
	glClearDepthf(value);
	glClear(GL_DEPTH_BUFFER_BIT);
}

static void
comparison(struct mh_webgl_depth *depth, struct mh_webgl_target *source)
{
	struct mh_depth_probe probes[] = {{1, 1, 1, .5f}, {1, 1, 1, .49999997f},
		{1, 1, 1, .50000006f}, {0, -100, -100, -100},
		{1, 2.9f, 1.9f, .5f}, {1, .9f, .9f, 0}};

	unsigned char visible[] = {9, 9, 9, 7, 9, 9};
	static const unsigned char expected[] = {0, 1, 0, 7, 1, 0};
	struct mh_error error;
	GLint framebuffer, viewport[4], scissor[4];
	GLuint pack;
	GLint binding, alignment, row_length, skip_rows, skip_pixels;

	clear_depth(source, .5f);

	/* Top-down pixel (2, 1), deliberately asymmetric vertically. */
	glEnable(GL_SCISSOR_TEST);
	glScissor(2, 4, 1, 1);
	glClearDepthf(.75f);
	glClear(GL_DEPTH_BUFFER_BIT);
	glScissor(0, 5, 1, 1);
	glClearDepthf(0);
	glClear(GL_DEPTH_BUFFER_BIT);
	glScissor(3, 2, 1, 1);
	glViewport(2, 3, 4, 5);
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	if (!operation(mh_webgl_depth_queue(depth, source, probes, 6, &error),
		&error))
		return;

	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
	glGetIntegerv(GL_VIEWPORT, viewport);
	glGetIntegerv(GL_SCISSOR_BOX, scissor);
	check((GLuint)framebuffer == source->framebuffer && viewport[0] == 2 &&
			viewport[1] == 3 && viewport[2] == 4 &&
			viewport[3] == 5 && glIsEnabled(GL_SCISSOR_TEST) &&
			scissor[0] == 3 && scissor[1] == 2 && scissor[2] == 1 &&
			scissor[3] == 1,
		"depth pass restores framebuffer, viewport and scissor");
	check(mh_webgl_depth_queue(depth, source, probes, 6, NULL) ==
			MH_ERROR_STATE,
		"unread visibility cannot be overwritten");
	check(mh_webgl_depth_read(depth, visible, 5, NULL) ==
				MH_ERROR_INVALID &&
			visible[0] == 9,
		"mismatched history preserves pending results and output");
	check(mh_webgl_depth_read(depth, NULL, 6, NULL) == MH_ERROR_INVALID,
		"missing visibility output preserves pending results");

	/* Queued results must survive changes to both borrowed inputs. */
	memset(probes, 0, sizeof(probes));
	clear_depth(source, 1);
	glGenBuffers(1, &pack);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, pack);
	glBufferData(GL_PIXEL_PACK_BUFFER, 128, NULL, GL_STREAM_READ);
	glPixelStorei(GL_PACK_ALIGNMENT, 8);
	glPixelStorei(GL_PACK_ROW_LENGTH, 11);
	glPixelStorei(GL_PACK_SKIP_ROWS, 2);
	glPixelStorei(GL_PACK_SKIP_PIXELS, 3);

	if (operation(mh_webgl_depth_read(depth, visible, 6, &error), &error))
		check(memcmp(visible, expected, sizeof(expected)) == 0,
			"queued depths retain binary32 strict comparison, top-down truncation and inactive history");

	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &binding);
	glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
	glGetIntegerv(GL_PACK_ROW_LENGTH, &row_length);
	glGetIntegerv(GL_PACK_SKIP_ROWS, &skip_rows);
	glGetIntegerv(GL_PACK_SKIP_PIXELS, &skip_pixels);
	check((GLuint)binding == pack && alignment == 8 && row_length == 11 &&
			skip_rows == 2 && skip_pixels == 3,
		"depth read preserves pixel-pack bindings and layout");
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glDeleteBuffers(1, &pack);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_SKIP_ROWS, 0);
	glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
	visible[0] = 17;
	check(mh_webgl_depth_read(depth, visible, 6, NULL) == MH_ERROR_NONE &&
			visible[0] == 17,
		"consumed results leave subsequent history unchanged");
}

static void
invalid_queries(struct mh_webgl_depth *depth, struct mh_webgl_target *source)
{
	static const struct mh_depth_probe invalid[] = {{2, 1, 1, .5f},
		{-1, 1, 1, .5f}, {1, 0, 1, .5f}, {1, 8, 1, .5f}, {1, 1, 0, .5f},
		{1, 1, 6, .5f}, {1, 1, 1, -.01f}, {1, 1, 1, 1.01f}};

	struct mh_depth_probe probe;
	unsigned char visible;
	size_t i;
	int success;
	float nan;
	volatile float zero;

	success = 1;

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
		if (mh_webgl_depth_queue(depth, source, &invalid[i], 1, NULL) !=
			MH_ERROR_INVALID)
			success = 0;

	check(success,
		"invalid active flags, coordinates and depths are rejected");
	zero = 0;
	nan = zero / zero;
	probe.active = 1;
	probe.x = nan;
	probe.y = 1;
	probe.depth = .5f;
	success = mh_webgl_depth_queue(depth, source, &probe, 1, NULL) ==
		MH_ERROR_INVALID;
	probe.x = 1;
	probe.y = nan;
	success = success &&
		mh_webgl_depth_queue(depth, source, &probe, 1, NULL) ==
			MH_ERROR_INVALID;
	probe.y = 1;
	probe.depth = nan;
	success = success &&
		mh_webgl_depth_queue(depth, source, &probe, 1, NULL) ==
			MH_ERROR_INVALID;
	check(success,
		"NaN coordinates and depths are rejected before submission");
	probe.active = 0;
	visible = 23;
	check(mh_webgl_depth_queue(depth, source, &probe, 1, NULL) ==
				MH_ERROR_NONE &&
			mh_webgl_depth_read(depth, &visible, 1, NULL) ==
				MH_ERROR_NONE &&
			visible == 23 &&
			mh_webgl_depth_queue(depth, source, NULL, 0, NULL) ==
				MH_ERROR_NONE,
		"inactive and empty queries create no pending batch");
	check(mh_webgl_depth_queue(depth, source, NULL, 1, NULL) ==
				MH_ERROR_INVALID &&
			mh_webgl_depth_queue(depth, source, &probe, 100001,
				NULL) == MH_ERROR_INVALID &&
			mh_webgl_depth_queue(depth, &depth->results, &probe, 1,
				NULL) == MH_ERROR_INVALID,
		"missing probes, excess count and color-only source are rejected");
}

static void
many_queries(struct mh_webgl_depth *depth, struct mh_webgl_target *source)
{
	struct mh_depth_probe *probes;
	unsigned char *visible;
	struct mh_error error;
	size_t count, i;
	int success;

	count = 100000;
	probes = (struct mh_depth_probe *)malloc(count * sizeof(*probes));
	visible = (unsigned char *)malloc(count);

	if (probes == NULL || visible == NULL) {
		check(0, "allocate depth limit fixture");
		free(probes);
		free(visible);
		return;
	}

	for (i = 0; i < count; i++) {
		probes[i].active = i % 3 != 0;
		probes[i].x = probes[i].y = 1;
		probes[i].depth = i % 2 ? .25f : .75f;
		visible[i] = 11;
	}

	clear_depth(source, .5f);

	if (operation(
		mh_webgl_depth_queue(depth, source, probes, count, &error),
		&error) &&
		operation(mh_webgl_depth_read(depth, visible, count, &error),
			&error)) {
		success = 1;

		for (i = 0; i < count; i++)
			if (visible[i] != (i % 3 == 0 ? 11 : i % 2)) {
				fprintf(stderr, "depth query %lu: got %u\n",
					(unsigned long)i, visible[i]);
				success = 0;
				break;
			}

		check(success,
			"maximum query count crosses texture rows without changing history order");
	}

	probes[0].active = 1;
	probes[0].depth = .25f;
	check(mh_webgl_depth_queue(depth, source, probes, 1, NULL) ==
				MH_ERROR_NONE &&
			mh_webgl_depth_read(depth, visible, 1, NULL) ==
				MH_ERROR_NONE &&
			visible[0] == 1,
		"smaller batches reuse grown storage without reading stale results");
	free(probes);
	free(visible);
}

int	mh_webgl_depth_check(void);

int
mh_webgl_depth_check(void)
{
	EmscriptenWebGLContextAttributes attributes;
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context;
	struct mh_webgl_depth depth;
	struct mh_webgl_target source;
	struct mh_error error;
	GLuint program, target;
	struct mh_depth_probe probe = {1, 1, 1, 0};

	failures = 0;
	memset(&depth, 0, sizeof(depth));
	memset(&source, 0, sizeof(source));
	emscripten_webgl_init_context_attributes(&attributes);
	attributes.majorVersion = 2;
	attributes.antialias = EM_FALSE;
	context = emscripten_webgl_create_context("#canvas", &attributes);

	if (context <= 0 ||
		emscripten_webgl_make_context_current(context) !=
			EMSCRIPTEN_RESULT_SUCCESS)
		return 1;

	if (operation(
		mh_webgl_target_create(&source, 8, 6, 1, &error), &error)) {
		comparison(&depth, &source);
		invalid_queries(&depth, &source);
		many_queries(&depth, &source);
		program = depth.program.handle;
		target = depth.results.framebuffer;
		mh_webgl_depth_destroy(&depth);
		mh_webgl_depth_destroy(&depth);
		check(!glIsProgram(program) && !glIsFramebuffer(target),
			"repeated destruction releases depth resources");
		check(mh_webgl_depth_queue(&depth, &source, &probe, 1, NULL) ==
				MH_ERROR_NONE,
			"destroyed depth resources can be recreated");
	}

	mh_webgl_depth_destroy(&depth);
	mh_webgl_target_destroy(&source);
	check(glGetError() == GL_NO_ERROR,
		"depth queries leave no WebGL errors");
	emscripten_webgl_destroy_context(context);

	return failures != 0;
}

int
main(void)
{
	return 0;
}

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE poll_context;
static struct mh_webgl_depth poll_depth;
static struct mh_webgl_target poll_source;
static unsigned char poll_visible[2];

int	mh_webgl_depth_poll_check(int phase);

int
mh_webgl_depth_poll_check(int phase)
{
	EmscriptenWebGLContextAttributes attributes;
	struct mh_depth_probe probes[2] = {{1, 1, 1, .25f}, {0, 0, 0, 0}};
	struct mh_error error;

	if (phase == 0) {
		failures = 0;
		memset(&poll_depth, 0, sizeof(poll_depth));
		memset(&poll_source, 0, sizeof(poll_source));
		poll_visible[0] = poll_visible[1] = 7;
		emscripten_webgl_init_context_attributes(&attributes);
		attributes.majorVersion = 2;
		attributes.antialias = EM_FALSE;
		poll_context =
			emscripten_webgl_create_context("#canvas", &attributes);

		if (poll_context <= 0 ||
			emscripten_webgl_make_context_current(poll_context) !=
				EMSCRIPTEN_RESULT_SUCCESS)
			return -1;

		if (!operation(mh_webgl_target_create(
				&poll_source, 8, 6, 1, &error),
			&error))
			return -1;

		clear_depth(&poll_source, .5f);

		if (!operation(mh_webgl_depth_queue(&poll_depth, &poll_source,
				probes, 2, &error),
			&error))
			return -1;

		check(mh_webgl_depth_poll(&poll_depth, poll_visible, 2, NULL) ==
					MH_ERROR_NONE &&
				poll_depth.count == 2 && poll_visible[0] == 7 &&
				poll_visible[1] == 7,
			"nonblocking poll preserves history before the browser yields");

		/*
		 * The download must retain its snapshot, not read a later
		 * scene.
		 */
		clear_depth(&poll_source, 0);

		return failures ? -1 : 0;
	}

	if (phase == 1) {
		if (!operation(mh_webgl_depth_poll(
				&poll_depth, poll_visible, 2, &error),
			&error))
			return -1;

		return poll_depth.count != 0;
	}

	check(poll_depth.count == 0 && poll_visible[0] == 1 &&
			poll_visible[1] == 7,
		"asynchronous download publishes its snapshot and preserves inactive history");
	mh_webgl_depth_destroy(&poll_depth);
	mh_webgl_target_destroy(&poll_source);
	check(glGetError() == GL_NO_ERROR,
		"asynchronous download closes without WebGL errors");
	emscripten_webgl_destroy_context(poll_context);

	return failures ? -1 : 0;
}
