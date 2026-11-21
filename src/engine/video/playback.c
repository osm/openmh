#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/video/layout.h"
#include "engine/video/playback.h"
#include "engine/window/title.h"
#include "interfaces/audio/device.h"
#include "interfaces/clock/clock.h"
#include "interfaces/input/input.h"
#include "interfaces/render/canvas.h"
#include "interfaces/video/backend.h"
#include "interfaces/video/decoder.h"
#include "interfaces/video/source.h"
#include "interfaces/window/window.h"

enum {
	LOGICAL_WIDTH = 640,
	LOGICAL_HEIGHT = 480,
	WINDOW_WIDTH = 960,
	WINDOW_HEIGHT = 720,
	AUDIO_CHANNELS = 2,
	AUDIO_BITS = 16,
	AUDIO_RATE_MIN = 8000,
	AUDIO_RATE_MAX = 192000,
	FRAME_COUNT_MAX = 100000,
	FRAME_INTERVAL_MIN_US = 1000,
	FRAME_INTERVAL_MAX_US = 1000000
};

#define MICROSECONDS_PER_SECOND 1000000.0
#define VIDEO_ERROR_SIZE 512

struct playback_provider {
	struct mh_video_provider	videos;
	const struct mh_video_services	*services;
	char				diagnostic[VIDEO_ERROR_SIZE];
};

enum video_phase {
	VIDEO_DRAW,
	VIDEO_WAIT,
	VIDEO_DRAIN
};

struct playback_player {
	struct mh_video_decoder		*decoder;
	struct mh_video_frame		frame;
	const struct mh_video_services	*services;
	struct mh_window		*window;
	struct mh_canvas		*canvas;
	int				window_acquired;
	int				fullscreen;
	struct mh_audio_device		*audio;
	unsigned long			width, height;
	unsigned long			frame_count;
	unsigned long			frame_index;
	unsigned long			capture_frame;
	unsigned long			delay_ms;
	const char			*capture_path;
	double				frame_interval_us;
	double				started_seconds;
	double				counter_frequency;
	enum mh_error_code		decode_status;
	int				frame_available;
	enum mh_video_outcome		outcome;
	enum video_phase		phase;
	enum mh_video_status		status;
	enum mh_error_code		failure_code;
	char				failure[VIDEO_ERROR_SIZE];
};

/* Close takes precedence when several terminal events arrive together. */
static enum mh_video_outcome
poll_events(const struct mh_input_provider *inputs)
{
	struct mh_event event;
	enum mh_video_outcome outcome;

	outcome = MH_VIDEO_NO_OUTCOME;

	while (mh_input_provider_event(inputs, &event)) {
		if (event.type == MH_EVENT_QUIT ||
			event.type == MH_EVENT_WINDOW_CLOSE)
			outcome = MH_VIDEO_CLOSED;
		else if (outcome != MH_VIDEO_CLOSED &&
			event.type == MH_EVENT_KEY_DOWN && !event.repeat &&
			(event.shortcut == MH_SHORTCUT_ESCAPE ||
				event.shortcut == MH_SHORTCUT_ENTER))
			outcome = MH_VIDEO_SKIPPED;
	}

	return outcome;
}

static void
close_player(struct playback_player *player)
{
	if (player == NULL)
		return;

	mh_audio_device_close(player->audio);

	mh_canvas_destroy(player->canvas);

	if (player->window_acquired) {
		mh_window_destroy(player->window);
		mh_window_system_release(player->services->windows);
	}

	mh_video_decoder_close(player->decoder);
	free(player);
}

static int
read_format(struct playback_player *player, unsigned long *audio_rate,
	unsigned int *x, unsigned int *y)
{
	const struct mh_video_decode_info *info;

	info = mh_video_decoder_info(player->decoder);
	player->width = info->width;
	player->height = info->height;
	player->frame_count = info->frames;
	player->frame_interval_us = info->interval_us;

	if (player->width > LOGICAL_WIDTH || player->height > LOGICAL_HEIGHT ||
		player->frame_count > FRAME_COUNT_MAX ||
		!(player->frame_interval_us >= FRAME_INTERVAL_MIN_US &&
			player->frame_interval_us <= FRAME_INTERVAL_MAX_US) ||
		(player->capture_path != NULL &&
			player->capture_frame >= player->frame_count) ||
		(info->audio_rate != 0 &&
			(info->audio_channels != AUDIO_CHANNELS ||
				info->audio_bits != AUDIO_BITS ||
				info->audio_rate < AUDIO_RATE_MIN ||
				info->audio_rate > AUDIO_RATE_MAX)) ||
		mh_video_origin(info->width, info->height, LOGICAL_WIDTH,
			LOGICAL_HEIGHT, x, y, NULL) != MH_ERROR_NONE)
		return 0;

	*audio_rate = info->audio_rate;

	return 1;
}

/* Keep a failed prefetched frame until the next draw reports it. */
static void
read_frame(struct playback_player *player)
{
	struct mh_error error;

	player->decode_status = mh_video_decoder_read(player->decoder,
		&player->frame, &player->frame_available, &error);

	if (player->decode_status != MH_ERROR_NONE)
		mh_error_copy(&error, &error, player->failure,
			sizeof(player->failure));
}

static double
counter_value(struct mh_clock_counter words)
{
	return (double)words.high * (double)MH_U32_MODULUS + (double)words.low;
}

static double
current_seconds(const struct playback_player *player)
{
	return counter_value(mh_clock_source_counter(player->services->clock)) /
		player->counter_frequency;
}

static enum mh_error_code
open_window(struct playback_player *player, struct mh_error *error_out)
{
	const struct mh_video_services *services;
	struct mh_window_system_options window_options;
	enum mh_error_code status;

	if (player->window != NULL) {
		(void)mh_window_set_title(player->window,
			mh_window_title(
				player->window, "Motorhead", "Motorhead movie"),
			NULL);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	services = player->services;
	window_options.offscreen = player->capture_path != NULL;
	window_options.preserve_environment = 0;
	status = mh_window_system_acquire(
		services->windows, &window_options, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	player->window_acquired = 1;

	return mh_window_open(services->windows,
		mh_window_title(player->window, "Motorhead", "Motorhead movie"),
		WINDOW_WIDTH, WINDOW_HEIGHT,
		player->capture_path != NULL ? MH_WINDOW_HIDDEN
					: MH_WINDOW_RESIZABLE |
				(player->fullscreen ? MH_WINDOW_FULLSCREEN : 0),
		&player->window, error_out);
}

static enum mh_error_code
open_presentation(struct playback_player *player, unsigned int x,
	unsigned int y, struct mh_error *error_out)
{
	struct mh_canvas_layout layout;
	enum mh_error_code status;

	status = open_window(player, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	layout.width = (unsigned int)player->width;
	layout.height = (unsigned int)player->height;
	layout.screen_width = LOGICAL_WIDTH;
	layout.screen_height = LOGICAL_HEIGHT;
	layout.x = x;
	layout.y = y;
	status = mh_canvas_open_layout(player->services->canvases,
		player->window, &layout, &player->canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_audio(struct playback_player *player, unsigned long rate, int mute,
	struct mh_error *error_out)
{
	struct mh_audio_format format;

	if (rate == 0 || mute || player->capture_path != NULL ||
		player->services->audio == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	format.encoding = MH_AUDIO_S16LE;
	format.rate = (unsigned int)rate;
	format.channels = AUDIO_CHANNELS;

	return mh_audio_device_open_format(player->services->audio, &format,
		NULL, &player->audio, error_out);
}

static enum mh_error_code
open_player(struct mh_video_decoder *decoder,
	struct playback_provider *provider,
	const struct mh_video_options *options, void **output,
	struct mh_error *error_out)
{
	struct playback_player *player;
	const struct mh_video_services *services;
	struct mh_error error;
	enum mh_error_code status;
	unsigned long audio_rate;
	unsigned int x, y;

	if (decoder == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot decode movie payload");

	services = provider->services;
	player = (struct playback_player *)calloc(1, sizeof(*player));

	if (player == NULL) {
		mh_video_decoder_close(decoder);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate movie player");
	}

	player->decoder = decoder;
	player->services = services;
	player->window = options->window;
	player->capture_path = options->capture;
	player->capture_frame = options->capture_frame;
	player->fullscreen = options->fullscreen;
	player->status = MH_VIDEO_FAILED;

	if (!read_format(player, &audio_rate, &x, &y)) {
		mh_video_decoder_close(player->decoder);
		free(player);
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported movie format");
	}

	status = open_presentation(player, x, y, &error);

	if (status != MH_ERROR_NONE)
		goto failed;

	status = open_audio(player, audio_rate, options->mute, &error);

	if (status != MH_ERROR_NONE)
		goto failed;

	player->counter_frequency =
		counter_value(mh_clock_source_frequency(services->clock));

	if (player->counter_frequency <= 0) {
		mh_error_set(&error, MH_ERROR_INVALID,
			"invalid movie clock frequency");
		goto failed;
	}

	read_frame(player);
	player->started_seconds = current_seconds(player);

	player->status = MH_VIDEO_RUNNING;
	player->phase = VIDEO_DRAW;
	*output = player;
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

failed:
	mh_error_copy(&error, &error, provider->diagnostic,
		sizeof(provider->diagnostic));
	close_player(player);

	return mh_error_forward(error_out, &error);
}

static enum mh_error_code
queue_audio(struct playback_player *player, struct mh_error *error_out)
{
	const unsigned char *audio;
	size_t bytes;
	enum mh_error_code status;

	if (player->audio == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	bytes = player->frame.pcm_bytes;
	audio = player->frame.pcm;

	if (bytes > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"movie audio chunk exceeds output limits");

	if (bytes != 0) {
		status = mh_audio_device_write(
			player->audio, audio, (size_t)bytes, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (player->frame_index == 0)
		return mh_audio_device_start(player->audio, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
capture_frame(struct playback_player *player, struct mh_error *error_out)
{
	struct mh_image image;
	enum mh_error_code status;

	if (player->services->capture == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"movie capture is unavailable");

	status = mh_canvas_read(player->canvas, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = player->services->capture(player->services->context, &image,
		player->capture_path, error_out);
	free(image.rgba);

	if (status == MH_ERROR_NONE) {
		player->outcome = MH_VIDEO_ENDED;
		player->status = MH_VIDEO_DONE;
	}

	return status;
}

static enum mh_error_code
draw_frame(struct playback_player *player, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (player->decode_status != MH_ERROR_NONE)
		return mh_error_set(
			error_out, player->decode_status, player->failure);

	if (!player->frame_available)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot decode movie frame");

	status = queue_audio(player, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_canvas_draw(player->canvas, player->frame.rgba, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (player->capture_path != NULL &&
		player->frame_index == player->capture_frame)
		return capture_frame(player, error_out);

	return mh_canvas_present(player->canvas, error_out);
}

static enum mh_video_status
step_player(struct playback_player *player)
{
	double now;
	struct mh_error error;
	int drained;

	if (player == NULL)
		return MH_VIDEO_FAILED;

	player->delay_ms = 0;

	if (player->status != MH_VIDEO_RUNNING)
		return player->status;

	if (player->phase == VIDEO_DRAW) {
		if (draw_frame(player, &error) != MH_ERROR_NONE)
			goto failed;

		if (player->status == MH_VIDEO_DONE)
			return player->status;

		if (player->capture_path != NULL) {
			read_frame(player);
			player->frame_index++;
			return MH_VIDEO_RUNNING;
		}

		player->phase = VIDEO_WAIT;
	}

	if (player->phase == VIDEO_WAIT) {
		player->outcome = poll_events(player->services->inputs);
		now = current_seconds(player);

		if (player->outcome != MH_VIDEO_NO_OUTCOME)
			goto finished;

		if (now - player->started_seconds < (player->frame_index + 1) *
				player->frame_interval_us /
				MICROSECONDS_PER_SECOND) {
			player->delay_ms = 1;
			return MH_VIDEO_RUNNING;
		}

		if (player->frame_index + 1 < player->frame_count) {
			read_frame(player);
			player->frame_index++;
			player->phase = VIDEO_DRAW;
			return MH_VIDEO_RUNNING;
		}

		if (player->audio == NULL)
			goto finished;

		/*
		 * Expose the resampler tail before polling stream consumption.
		 */
		if (mh_audio_device_flush(player->audio, &error) !=
			MH_ERROR_NONE)
			goto failed;

		player->phase = VIDEO_DRAIN;
	}

	player->outcome = poll_events(player->services->inputs);

	if (player->outcome != MH_VIDEO_NO_OUTCOME)
		goto finished;

	if (mh_audio_device_drained(player->audio, &drained, &error) !=
		MH_ERROR_NONE)
		goto failed;

	if (!drained) {
		player->delay_ms = 1;
		return MH_VIDEO_RUNNING;
	}

finished:
	if (player->outcome == MH_VIDEO_NO_OUTCOME)
		player->outcome = MH_VIDEO_ENDED;

	player->status = MH_VIDEO_DONE;
	return player->status;

failed:
	/*
	 * Retain diagnostics before another provider operation can replace
	 * them.
	 */
	mh_error_copy(&error, &error, player->failure, sizeof(player->failure));
	player->failure_code = error.code;
	player->status = MH_VIDEO_FAILED;

	return player->status;
}

static enum mh_error_code
open_video(void *context, const struct mh_video_options *options, void **output,
	struct mh_error *error_out)
{
	struct mh_file data;
	enum mh_error_code status;

	struct mh_video_decoder *decoder;
	struct playback_provider *provider;
	int audio;

	provider = context;

	status = mh_video_source_read(provider->services->clips,
		options->resources, options->reference, options->collection,
		options->index, &data, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	audio = !options->mute && options->capture == NULL &&
		provider->services->audio != NULL;
	status = mh_video_decoder_open(provider->services->decoder, data.data,
		data.size, audio, &decoder, error_out);
	free(data.data);

	if (status != MH_ERROR_NONE)
		return status;

	return open_player(decoder, context, options, output, error_out);
}

static enum mh_error_code
resolve_video(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, char *output, size_t capacity,
	int *found, struct mh_error *error_out)
{
	struct playback_provider *provider;

	provider = context;

	return mh_video_source_resolve(provider->services->clips, resources,
		root, reference, output, capacity, found, error_out);
}

static enum mh_video_status
step_video(void *context, void *token, unsigned long *delay,
	enum mh_video_outcome *outcome, struct mh_error *error_out)
{
	struct playback_player *player;
	enum mh_video_status status;

	(void)context;
	player = token;
	status = step_player(player);
	*delay = player->delay_ms;
	*outcome = player->outcome;
	mh_error_set(error_out,
		status == MH_VIDEO_FAILED ? player->failure_code
					: MH_ERROR_NONE,
		status == MH_VIDEO_FAILED
			? (player->failure[0] != 0 ? player->failure
						: "movie playback failed")
			: NULL);

	return status;
}

static void
close_video(void *context, void *token)
{
	(void)context;
	close_player(token);
}

enum mh_error_code
mh_video_playback_open(const struct mh_video_services *services,
	const struct mh_video_provider **output, struct mh_error *error_out)
{
	static const struct mh_video_ops ops = {
		open_video, step_video, close_video, resolve_video};

	struct playback_provider *provider;

	if (output == NULL || services == NULL || services->windows == NULL ||
		services->canvases == NULL || services->inputs == NULL ||
		services->clock == NULL || services->decoder == NULL ||
		services->clips == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete movie services");

	provider = calloc(1, sizeof(*provider));

	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate movie provider");

	provider->services = services;
	mh_video_provider_init(&provider->videos, &ops, provider, NULL);
	*output = &provider->videos;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_video_playback_close(const struct mh_video_provider *provider)
{
	if (provider != NULL)
		free(provider->context);
}
