#include <string.h>

#include "backends/render/webgl_error.h"
#include "backends/render/webgl_history.h"

static enum mh_error_code
idle(const struct mh_webgl_history *history, struct mh_error *error_out)
{
	if (history == NULL || history->front.target.framebuffer == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL frame history");

	if (history->drawing)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL frame is already in progress");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_history_create(struct mh_webgl_history *history, unsigned int width,
	unsigned int height, struct mh_error *error_out)
{
	if (history == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL frame history output");

	return mh_webgl_target_create(
		&history->front.target, width, height, 1, error_out);
}

static enum mh_error_code
create_slot(const struct mh_webgl_history *history,
	struct mh_webgl_history_slot *slot, struct mh_error *error_out)
{
	return mh_webgl_target_create(&slot->target,
		history->front.target.color.width,
		history->front.target.color.height, 1, error_out);
}

enum mh_error_code
mh_webgl_history_frames(struct mh_webgl_history *history, unsigned int frames,
	struct mh_error *error_out)
{
	struct mh_webgl_history_slot back, third;
	enum mh_error_code status;

	status = idle(history, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (frames != 2 && frames != 3)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"WebGL blur history requires two or three frames");

	if (history->back.target.framebuffer != 0 &&
		(frames == 3) == (history->third.target.framebuffer != 0))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	memset(&back, 0, sizeof(back));
	memset(&third, 0, sizeof(third));

	if (history->back.target.framebuffer == 0)
		status = create_slot(history, &back, error_out);

	if (status == MH_ERROR_NONE && frames == 3)
		status = create_slot(history, &third, error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_target_destroy(&back.target);
		mh_webgl_target_destroy(&third.target);
		return status;
	}

	if (back.target.framebuffer != 0)
		history->back = back;

	if (frames == 3)
		history->third = third;
	else {
		mh_webgl_target_destroy(&history->third.target);
		history->third.ready = 0;
	}

	history->front.ready = history->back.ready = history->third.ready = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_history_blur(struct mh_webgl_history *history, int enabled,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = idle(history, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (enabled != 0 && enabled != 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL motion blur setting");

	if (enabled && history->back.target.framebuffer == 0) {
		status = create_slot(history, &history->back, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	history->blur = enabled;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static struct mh_webgl_history_slot *
draw_slot(struct mh_webgl_history *history)
{
	return history->back.target.framebuffer != 0 ? &history->back
						: &history->front;
}

static void
begin_target(struct mh_webgl_history_slot *slot, int scene, int blur,
	const float fog[3])
{
	float color[3];
	GLbitfield clear;

	color[0] = scene ? .055f : 0;
	color[1] = scene ? .070f : 0;
	color[2] = scene ? .095f : 0;

	if (scene && fog != NULL)
		memcpy(color, fog, sizeof(color));

	clear = GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT;

	if (scene && blur) {
		color[0] = color[1] = color[2] = 0;

		if (slot->ready)
			clear = GL_DEPTH_BUFFER_BIT;
	}

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, slot->target.framebuffer);
	glViewport(0, 0, (GLsizei)slot->target.color.width,
		(GLsizei)slot->target.color.height);
	glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glDepthRangef(0, 1);
	glClearColor(color[0], color[1], color[2], 1);
	glClearDepthf(1);
	glClear(clear);
	slot->ready = 0;
}

enum mh_error_code
mh_webgl_history_begin(struct mh_webgl_history *history, int scene,
	const float fog[3], const struct mh_webgl_target **target,
	struct mh_error *error_out)
{
	struct mh_webgl_history_slot *slot;
	enum mh_error_code status;

	status = idle(history, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (target == NULL || (scene != 0 && scene != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL frame request");

	slot = draw_slot(history);
	begin_target(slot, scene, history->blur, fog);
	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE)
		return status;

	history->drawing = 1;
	*target = &slot->target;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_history_finish(
	struct mh_webgl_history *history, struct mh_error *error_out)
{
	struct mh_webgl_history_slot completed;
	enum mh_error_code status;

	if (history == NULL || !history->drawing)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"no WebGL frame is in progress");

	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_history_cancel(history);
		return status;
	}

	if (history->back.target.framebuffer != 0) {
		completed = history->back;

		if (history->third.target.framebuffer != 0) {
			history->back = history->third;
			history->third = history->front;
		} else
			history->back = history->front;

		history->front = completed;
	}

	history->front.ready = 1;
	history->drawing = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_webgl_history_cancel(struct mh_webgl_history *history)
{
	if (history != NULL)
		history->drawing = 0;
}

void
mh_webgl_history_destroy(struct mh_webgl_history *history)
{
	if (history == NULL)
		return;

	mh_webgl_target_destroy(&history->front.target);
	mh_webgl_target_destroy(&history->back.target);
	mh_webgl_target_destroy(&history->third.target);
	memset(history, 0, sizeof(*history));
}
