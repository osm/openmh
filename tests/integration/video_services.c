#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_resource.h"
#include "backends/video/divi_provider.h"
#include "backends/video/ffmpeg_provider.h"
#include "engine/video/playback.h"
#include "interfaces/audio/backend.h"
#include "interfaces/clock/backend.h"
#include "interfaces/input/backend.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/video/backend.h"
#include "interfaces/video/decoder_backend.h"
#include "interfaces/video/source_backend.h"
#include "interfaces/window/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "movie services line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

enum playback_case {
	PLAY_TO_END,
	SKIP_PLAYBACK,
	CANCEL_PLAYBACK
};

struct fixture {
	struct mh_video_decoder	*reference;
	struct mh_file		encoded;
	struct mh_video_frame	expected;
	unsigned long		width, height, frames, now;
	unsigned int	acquired, released, windows, canvases, draws, presents;
	int			bad, fail_open, event;
	char			diagnostic[64];
	unsigned long		audio_rate;
	unsigned int		audio_opens, audio_closes, writes, starts;
	int			audio_active, flushed, drained, fail_start;
	unsigned int		decoder_opens, decoder_closes;
	int			fail_decode;
};

struct sample_decoder {
	unsigned int	frame;
	unsigned char	rgba[16];
	int		audio;
};

static int
reset_reference(struct fixture *fixture)
{
	int available;

	mh_video_decoder_close(fixture->reference);
	fixture->reference = NULL;
	CHECK(mh_video_decoder_open(mh_video_ffmpeg_decoder(),
		fixture->encoded.data, fixture->encoded.size, 1,
		&fixture->reference, NULL) == MH_ERROR_NONE);
	CHECK(mh_video_decoder_read(fixture->reference, &fixture->expected,
		&available, NULL) == MH_ERROR_NONE &&
		available);

	return 0;
}

static enum mh_error_code
acquire(void *context, const struct mh_window_system_options *options,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;

	if (options == NULL || options->offscreen)
		fixture->bad = 1;

	fixture->acquired++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
release(void *context)
{
	struct fixture *fixture;

	fixture = context;

	if (fixture->windows != 0 || fixture->canvases != 0 ||
		fixture->audio_active)
		fixture->bad = 1;

	fixture->released++;
	strcpy(fixture->diagnostic, "video released");
}

static enum mh_error_code
open_window(void *context, const char *title, unsigned int width,
	unsigned int height, unsigned int flags, void **output,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;

	if (strcmp(title, "Motorhead") != 0 || width != 960 || height != 720 ||
		flags != MH_WINDOW_RESIZABLE ||
		fixture->acquired != fixture->released + 1)
		fixture->bad = 1;

	fixture->windows++;
	*output = fixture;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_window(void *context, void *window)
{
	struct fixture *fixture;

	fixture = context;

	if (window != fixture || fixture->canvases != 0)
		fixture->bad = 1;

	fixture->windows--;
	strcpy(fixture->diagnostic, "window closed");
}

static unsigned long
window_id(void *context, const void *window)
{
	(void)context;
	(void)window;

	return 1;
}

static const struct mh_window_ops window_ops = {acquire, release, open_window,
	close_window, window_id, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL};

static enum mh_error_code
open_canvas(void *context, struct mh_window *window,
	const struct mh_canvas_layout *layout, void **output,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;

	if (mh_window_backend_context(window, &window_ops) != fixture ||
		layout->width != fixture->width ||
		layout->height != fixture->height ||
		layout->screen_width != 640 || layout->screen_height != 480 ||
		layout->x != (640 - fixture->width) / 2 ||
		layout->y != (480 - fixture->height) / 2)
		fixture->bad = 1;

	if (fixture->fail_open) {
		strcpy(fixture->diagnostic,
			fixture->fail_open == 1 ? "selected canvas unavailable"
						: "other canvas unavailable");
		return mh_error_set(
			error_out, MH_ERROR_UNSUPPORTED, fixture->diagnostic);
	}

	fixture->canvases++;
	*output = fixture;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_canvas(void *context, void *canvas)
{
	struct fixture *fixture;

	fixture = context;

	if (canvas != fixture || fixture->windows != 1)
		fixture->bad = 1;

	fixture->canvases--;
}

static enum mh_error_code
draw(void *context, void *canvas, const unsigned char *rgba,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	int available;
	unsigned long n;

	fixture = context;

	if (canvas != fixture || fixture->canvases != 1)
		fixture->bad = 1;

	if (fixture->reference == NULL) {
		for (n = 0; n < fixture->width * fixture->height; n++)
			if (rgba[n * 4] != fixture->draws ||
				rgba[n * 4 + 1] != 12 ||
				rgba[n * 4 + 2] != 34 || rgba[n * 4 + 3] != 255)
				fixture->bad = 1;
	} else {
		if (memcmp(rgba, fixture->expected.rgba,
			fixture->expected.rgba_bytes) != 0)
			fixture->bad = 1;
	}

	fixture->draws++;

	if (fixture->reference != NULL && fixture->draws < fixture->frames)
		if (mh_video_decoder_read(fixture->reference,
			&fixture->expected, &available,
			NULL) != MH_ERROR_NONE ||
			!available)
			fixture->bad = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
present(void *context, void *canvas, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)canvas;
	fixture = context;
	fixture->presents++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static struct mh_clock_counter
counter(void *context)
{
	struct fixture *fixture;
	struct mh_clock_counter value;

	fixture = context;
	value.low = fixture->now;
	value.high = 1;

	return value;
}

static struct mh_clock_counter
frequency(void *context)
{
	struct mh_clock_counter value;

	(void)context;
	value.low = 1000;
	value.high = 0;

	return value;
}

static unsigned long
ticks(void *context)
{
	struct fixture *fixture;

	fixture = context;

	return fixture->now;
}

static int
event(void *context, void *input, struct mh_event *event)
{
	struct fixture *fixture;

	(void)input;
	fixture = context;

	if (!fixture->event)
		return 0;

	memset(event, 0, sizeof(*event));
	event->type = MH_EVENT_KEY_DOWN;
	event->shortcut = MH_SHORTCUT_ESCAPE;
	fixture->event = 0;

	return 1;
}

static enum mh_error_code
mouse_grab(void *context, void *token, int enabled, struct mh_error *error_out)
{
	(void)context;
	(void)token;
	(void)enabled;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_input(void *context, struct mh_window *window, int grab_mouse,
	void **output, struct mh_error *error_out)
{
	(void)grab_mouse;

	(void)context;

	(void)window;
	(void)output;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED, "events only");
}

static void
close_input(void *context, void *input)
{
	(void)context;
	(void)input;
}

static int
focused(void *context, const void *input)
{
	(void)context;
	(void)input;

	return 0;
}

static enum mh_error_code
poll_input(void *context, void *input, struct mh_keyboard *keyboard,
	const unsigned char down[MH_KEY_COUNT], unsigned long now,
	unsigned long mode, struct mh_device_input *snapshot,
	struct mh_error *error_out)
{
	(void)context;

	(void)input;
	(void)keyboard;
	(void)down;
	(void)now;
	(void)mode;
	(void)snapshot;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED, "events only");
}

static int
key_down(void *context, unsigned int scan)
{
	(void)context;
	(void)scan;

	return 0;
}

static void
characters(void *context, const unsigned char down[MH_KEY_COUNT],
	unsigned char output[MH_KEY_COUNT])
{
	(void)context;

	(void)down;
	memset(output, 0, MH_KEY_COUNT);
}

static enum mh_error_code
open_audio(void *context, const struct mh_audio_format *format,
	const struct mh_audio_callbacks *callbacks, void **output,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;

	if (format->encoding != MH_AUDIO_S16LE || format->channels != 2 ||
		format->rate != fixture->audio_rate ||
		callbacks->request != NULL || callbacks->service != NULL ||
		fixture->audio_active)
		fixture->bad = 1;

	fixture->audio_active = 1;
	fixture->flushed = fixture->drained = 0;
	fixture->audio_opens++;
	*output = fixture;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_audio(void *context, void *token)
{
	struct fixture *fixture;

	fixture = context;

	if (token != fixture || !fixture->audio_active ||
		fixture->canvases != 1)
		fixture->bad = 1;

	fixture->audio_active = 0;
	fixture->audio_closes++;
}

static enum mh_error_code
start_audio(void *context, void *token, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)token;
	fixture = context;

	if (fixture->fail_start)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"audio activation denied");

	if (fixture->writes == 0 || !fixture->audio_active)
		fixture->bad = 1;

	fixture->starts++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
write_audio(void *context, void *token, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)token;
	fixture = context;

	if (!fixture->audio_active || fixture->flushed)
		fixture->bad = 1;

	if (fixture->reference != NULL) {
		if (size != fixture->expected.pcm_bytes ||
			memcmp(bytes, fixture->expected.pcm, size) != 0)
			fixture->bad = 1;
	} else if (size != 4 || memcmp(bytes, "\0\100\0\300", 4) != 0)
		fixture->bad = 1;

	fixture->writes++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
flush_audio(void *context, void *token, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)token;
	fixture = context;

	if (fixture->flushed || fixture->draws != fixture->frames)
		fixture->bad = 1;

	fixture->flushed = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
drain_audio(
	void *context, void *token, int *drained, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)token;
	fixture = context;

	if (!fixture->flushed)
		fixture->bad = 1;

	*drained = fixture->drained;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
unused_audio(void *context, void *token, struct mh_error *error_out)
{
	(void)context;
	(void)token;

	return mh_error_set(
		error_out, MH_ERROR_UNSUPPORTED, "unused queue operation");
}

static enum mh_error_code
inspect_audio(void *context, void *token, struct mh_audio_device_state *state,
	struct mh_error *error_out)
{
	(void)state;

	return unused_audio(context, token, error_out);
}

static enum mh_error_code
pause_audio(void *context, void *token, int paused, struct mh_error *error_out)
{
	(void)paused;

	return unused_audio(context, token, error_out);
}

static enum mh_error_code
request_audio(void *context, void *token, void (*request)(void *, size_t),
	struct mh_error *error_out)
{
	(void)request;

	return unused_audio(context, token, error_out);
}

static int
check_playback(struct fixture *fixture, const struct mh_video_provider *videos,
	const struct mh_video_options *options, enum playback_case test)
{
	struct mh_video_player *player;
	enum mh_video_status status;
	unsigned int steps;
	int complete;

	fixture->draws = fixture->presents = 0;
	fixture->now = 1000;

	if (fixture->reference != NULL)
		CHECK(reset_reference(fixture) == 0);

	CHECK(mh_video_open_from(videos, options, &complete, &player, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_video_step(player) == MH_VIDEO_RUNNING);
	CHECK(fixture->draws == 1 && mh_video_delay(player) == 1);
	fixture->now += 39;
	CHECK(mh_video_step(player) == MH_VIDEO_RUNNING && fixture->draws == 1);

	if (test == PLAY_TO_END) {
		steps = 0;

		do {
			fixture->now += 40;
			status = mh_video_step(player);
			CHECK(++steps <= fixture->frames * 3);
		} while (status == MH_VIDEO_RUNNING && !fixture->flushed);

		CHECK(fixture->flushed && status == MH_VIDEO_RUNNING);
		CHECK(mh_video_step(player) == MH_VIDEO_RUNNING &&
			mh_video_delay(player) == 1);
		fixture->drained = 1;
		status = mh_video_step(player);

		CHECK(status == MH_VIDEO_DONE &&
			fixture->draws == fixture->frames);
		CHECK(mh_video_close(player) && complete == MH_VIDEO_ENDED);
	} else if (test == SKIP_PLAYBACK) {
		fixture->event = 1;
		CHECK(mh_video_step(player) == MH_VIDEO_DONE &&
			fixture->draws == 1);
		CHECK(mh_video_close(player) && complete == MH_VIDEO_SKIPPED);
	} else
		CHECK(!mh_video_close(player) &&
			complete == MH_VIDEO_NO_OUTCOME);

	CHECK(fixture->draws == fixture->presents && !fixture->bad);
	CHECK(fixture->acquired ==
		fixture->released + (options->window != NULL));

	return 0;
}

static int
check_borrowed_window(struct fixture *fixture,
	const struct mh_video_provider *videos,
	const struct mh_video_options *options,
	const struct mh_window_system *windows)
{
	struct mh_window_system_options window_options;
	struct mh_video_options borrowed;
	struct mh_video_player *player;
	unsigned int acquired;
	int complete;

	window_options.offscreen = 0;
	window_options.preserve_environment = 1;
	CHECK(mh_window_system_acquire(windows, &window_options, NULL) ==
		MH_ERROR_NONE);
	borrowed = *options;
	CHECK(mh_window_open(windows, "Motorhead", 960, 720,
		MH_WINDOW_RESIZABLE, &borrowed.window,
		NULL) == MH_ERROR_NONE);
	acquired = fixture->acquired;
	fixture->fail_open = 0;
	CHECK(check_playback(fixture, videos, &borrowed, PLAY_TO_END) == 0);
	CHECK(check_playback(fixture, videos, &borrowed, SKIP_PLAYBACK) == 0);
	CHECK(check_playback(fixture, videos, &borrowed, CANCEL_PLAYBACK) == 0);
	CHECK(fixture->windows == 1 && fixture->canvases == 0);
	CHECK(fixture->acquired == acquired);

	/*
	 * Failed presentation must also leave the application's window alive.
	 */
	fixture->fail_open = 1;
	player = NULL;
	CHECK(mh_video_open_from(videos, &borrowed, &complete, &player, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(player == NULL && fixture->windows == 1 &&
		fixture->canvases == 0);
	CHECK(fixture->acquired == acquired && !fixture->bad);
	fixture->fail_open = 0;
	mh_window_destroy(borrowed.window);
	mh_window_system_release(windows);
	CHECK(fixture->acquired == fixture->released && !fixture->bad);

	return 0;
}

static int
check_audio_failure(struct fixture *fixture,
	const struct mh_video_provider *videos,
	const struct mh_video_options *options)
{
	struct mh_video_player *player;
	struct mh_error error;
	int complete;

	fixture->draws = fixture->presents = 0;
	fixture->fail_start = 1;

	if (fixture->reference != NULL)
		CHECK(reset_reference(fixture) == 0);

	CHECK(mh_video_open_from(videos, options, &complete, &player, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_video_step(player) == MH_VIDEO_FAILED);
	CHECK(mh_video_error(player, &error) == MH_ERROR_PERMISSION);
	CHECK(strcmp(error.message, "audio activation denied") == 0);
	CHECK(!mh_video_close(player) && complete == MH_VIDEO_NO_OUTCOME);
	CHECK(fixture->audio_opens == fixture->audio_closes && !fixture->bad);
	fixture->fail_start = 0;

	return 0;
}

static int
check_silent_playback(struct fixture *fixture,
	const struct mh_video_provider *videos,
	const struct mh_video_options *options)
{
	struct mh_video_player *player;
	int complete;

	fixture->draws = fixture->presents = 0;

	if (fixture->reference != NULL)
		CHECK(reset_reference(fixture) == 0);

	CHECK(mh_video_open_from(videos, options, &complete, &player, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_video_step(player) == MH_VIDEO_RUNNING);
	CHECK(!mh_video_close(player));
	CHECK(fixture->audio_opens == 4 && !fixture->bad);

	return 0;
}

static int
check_open_failures(struct fixture *fixture,
	const struct mh_video_provider *videos,
	const struct mh_video_options *options,
	const struct mh_video_services *services)
{
	const struct mh_video_provider *other_videos;
	struct mh_video_player *player;
	struct mh_error error, retained;
	int complete;

	fixture->fail_open = 1;
	player = NULL;
	CHECK(mh_video_open_from(videos, options, &complete, &player, &error) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(player == NULL &&
		strcmp(error.message, "selected canvas unavailable") == 0);
	retained = error;
	CHECK(mh_video_playback_open(services, &other_videos, NULL) ==
		MH_ERROR_NONE);
	fixture->fail_open = 2;
	CHECK(mh_video_open_from(other_videos, options, &complete, &player,
		&error) == MH_ERROR_UNSUPPORTED);
	CHECK(player == NULL &&
		strcmp(error.message, "other canvas unavailable") == 0);
	CHECK(strcmp(retained.message, "selected canvas unavailable") == 0);
	CHECK(fixture->acquired == fixture->released && !fixture->bad);
	mh_video_playback_close(other_videos);

	return 0;
}

static enum mh_error_code
sample_open(void *context, const unsigned char *data, size_t size, int audio,
	void **output, struct mh_video_decode_info *info,
	struct mh_error *error_out)
{
	struct sample_decoder *decoder;
	struct fixture *fixture;

	fixture = context;

	if (size != 4 || memcmp(data, "TEST", 4) != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "test video bytes");

	decoder = calloc(1, sizeof(*decoder));

	if (decoder == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test allocation");

	decoder->audio = audio;
	info->width = info->height = 2;
	info->frames = 3;
	info->interval_us = 40000;
	info->audio_rate = 22050;
	info->audio_channels = 2;
	info->audio_bits = 16;
	fixture->decoder_opens++;
	*output = decoder;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sample_read(void *context, void *token, struct mh_video_frame *output,
	struct mh_error *error_out)
{
	struct sample_decoder *decoder;
	struct fixture *fixture;
	unsigned int i;

	fixture = context;
	decoder = token;

	if (fixture->fail_decode == 1 && decoder->frame == 1) {
		strcpy(fixture->diagnostic, "test frame failure");
		return mh_error_set(
			error_out, MH_ERROR_IO, fixture->diagnostic);
	}

	for (i = 0; i < 4; i++) {
		decoder->rgba[i * 4] = (unsigned char)decoder->frame;
		decoder->rgba[i * 4 + 1] = 12;
		decoder->rgba[i * 4 + 2] = 34;
		decoder->rgba[i * 4 + 3] = 255;
	}

	decoder->frame++;
	output->rgba = decoder->rgba;
	output->rgba_bytes =
		sizeof(decoder->rgba) - (fixture->fail_decode == 2);
	output->pcm = (const unsigned char *)"\0\100\0\300";
	output->pcm_bytes = decoder->audio ? 4 : 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
sample_close(void *context, void *token)
{
	struct fixture *fixture;

	fixture = context;
	fixture->decoder_closes++;
	strcpy(fixture->diagnostic, "test decoder closed");
	free(token);
}

static int
check_decoder_frames(const struct mh_video_decoder_provider *provider,
	struct fixture *fixture)
{
	struct mh_video_decoder *decoder;
	struct mh_video_frame frame, before;
	unsigned int i;
	int available;

	CHECK(mh_video_decoder_open(provider, (const unsigned char *)"TEST", 4,
		0, &decoder, NULL) == MH_ERROR_NONE);
	CHECK(mh_video_decoder_info(decoder)->frames == 3);

	for (i = 0; i < 3; i++) {
		CHECK(mh_video_decoder_read(decoder, &frame, &available,
			NULL) == MH_ERROR_NONE);
		CHECK(available && frame.rgba_bytes == 16 &&
			frame.rgba[0] == i && frame.pcm_bytes == 0);
	}

	before = frame;
	CHECK(mh_video_decoder_read(decoder, &frame, &available, NULL) ==
		MH_ERROR_NONE);
	CHECK(!available && frame.rgba == before.rgba &&
		frame.rgba_bytes == before.rgba_bytes);
	mh_video_decoder_close(decoder);
	fixture->fail_decode = 2;
	CHECK(mh_video_decoder_open(provider, (const unsigned char *)"TEST", 4,
		0, &decoder, NULL) == MH_ERROR_NONE);
	memset(&before, 0, sizeof(before));
	frame = before;
	available = 99;
	CHECK(mh_video_decoder_read(decoder, &frame, &available, NULL) ==
		MH_ERROR_BACKEND);
	CHECK(available == 99 && memcmp(&frame, &before, sizeof(frame)) == 0);
	mh_video_decoder_close(decoder);
	fixture->fail_decode = 0;
	CHECK(fixture->decoder_opens == fixture->decoder_closes);

	return 0;
}

static enum mh_error_code
sample_resolve(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, char *output, size_t capacity,
	int *found, struct mh_error *error_out)
{
	(void)context;

	(void)resources;

	if (strcmp(root, "library") != 0 ||
		strcmp(reference, "movies/movies.pak") != 0 ||
		capacity < sizeof("converted-video"))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "test collection lookup");

	strcpy(output, "converted-video");
	*found = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sample_clip(void *context, const struct mh_resource_source *resources,
	const char *reference, int collection, unsigned int index,
	struct mh_file *output, struct mh_error *error_out)
{
	(void)context;

	if (!collection || index != 0 ||
		strcmp(reference, "converted-video") != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "test clip selection");

	return mh_resource_source_read(
		resources, "library/scene.custom", output, error_out);
}

static int
check_alternate(struct fixture *fixture, struct mh_video_services *services)
{
	static const struct mh_video_decoder_ops ops = {
		sample_open, sample_read, sample_close};

	static const struct mh_video_source_ops source_ops = {
		sample_resolve, sample_clip};

	struct mh_video_decoder_provider decoder;
	struct mh_video_source_provider clips;
	char selection[128];
	int found;
	struct mh_resource_entry entry;
	const struct mh_resource_source *resources;
	const struct mh_video_provider *videos;
	struct mh_video_player *player;
	struct mh_video_options options;
	struct mh_error error;
	int complete;

	memset(fixture, 0, sizeof(*fixture));
	fixture->width = fixture->height = 2;
	fixture->frames = 3;
	fixture->audio_rate = 22050;
	decoder.ops = &ops;
	decoder.context = fixture;
	services->decoder = &decoder;
	clips.ops = &source_ops;
	clips.context = NULL;
	services->clips = &clips;
	entry.path = "library/scene.custom";
	entry.data = (const unsigned char *)"TEST";
	entry.size = 4;
	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_video_playback_open(services, &videos, NULL) == MH_ERROR_NONE);
	memset(&options, 0, sizeof(options));
	options.resources = resources;
	CHECK(mh_video_resolve_from(videos, resources, "library",
		"movies/movies.pak", selection, sizeof(selection), &found,
		NULL) == MH_ERROR_NONE);
	CHECK(found);
	options.reference = selection;
	options.collection = 1;
	CHECK(check_playback(fixture, videos, &options, PLAY_TO_END) == 0);
	CHECK(check_playback(fixture, videos, &options, SKIP_PLAYBACK) == 0);
	CHECK(check_playback(fixture, videos, &options, CANCEL_PLAYBACK) == 0);
	CHECK(fixture->decoder_opens == 3 && fixture->decoder_closes == 3);
	CHECK(check_decoder_frames(&decoder, fixture) == 0);

	fixture->draws = fixture->presents = 0;
	fixture->fail_decode = 1;
	CHECK(mh_video_open_from(videos, &options, &complete, &player, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_video_step(player) == MH_VIDEO_RUNNING);
	fixture->now += 40;
	CHECK(mh_video_step(player) == MH_VIDEO_RUNNING);
	CHECK(mh_video_step(player) == MH_VIDEO_FAILED);
	CHECK(mh_video_error(player, &error) == MH_ERROR_IO);
	CHECK(strcmp(error.message, "test frame failure") == 0);
	CHECK(!mh_video_close(player) && complete == MH_VIDEO_NO_OUTCOME);
	CHECK(fixture->decoder_opens == fixture->decoder_closes &&
		!fixture->bad);
	CHECK(fixture->acquired == fixture->released);
	mh_video_playback_close(videos);
	mh_memory_resources_close(resources);

	return 0;
}

int
main(int argc, char *argv[])
{
	static const struct mh_canvas_ops canvas_ops = {
		open_canvas, close_canvas, draw, present, NULL, NULL};

	static const struct mh_clock_ops clock_ops = {
		counter, frequency, ticks, NULL, NULL};

	static const struct mh_input_ops input_ops = {open_input, close_input,
		focused, event, poll_input, key_down, characters, NULL,
		mouse_grab};

	static const struct mh_audio_device_ops audio_ops = {open_audio,
		close_audio, start_audio, unused_audio, unused_audio,
		unused_audio, write_audio, inspect_audio, pause_audio,
		unused_audio, request_audio, NULL, flush_audio, drain_audio};

	struct fixture fixture;
	struct mh_audio_provider audio;
	struct mh_window_system windows;
	struct mh_canvas_provider canvases;
	struct mh_clock_source clock;
	struct mh_input_provider inputs;
	struct mh_video_services services;
	const struct mh_video_provider *videos;
	struct mh_video_options options;
	struct mh_resource_entry entry;
	const struct mh_resource_source *resources;
	struct mh_file data;
	const struct mh_video_decode_info *info;

	CHECK(argc == 2);
	CHECK(mh_resource_source_read(mh_filesystem_resources(), argv[1], &data,
		NULL) == MH_ERROR_NONE);
	memset(&fixture, 0, sizeof(fixture));
	fixture.encoded = data;
	CHECK(reset_reference(&fixture) == 0);
	info = mh_video_decoder_info(fixture.reference);
	fixture.width = info->width;
	fixture.height = info->height;
	fixture.frames = info->frames;
	fixture.audio_rate = info->audio_rate;
	CHECK(info->interval_us == 40000.0);
	CHECK(mh_audio_provider_init(&audio, &audio_ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	entry.path = "headless.smk";
	entry.data = data.data;
	entry.size = data.size;
	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_window_system_init(&windows, &window_ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_canvas_provider_init(&canvases, &canvas_ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_clock_source_init(&clock, &clock_ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_input_provider_init(&inputs, &input_ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	memset(&services, 0, sizeof(services));
	services.clips = mh_video_divi_source();
	services.decoder = mh_video_ffmpeg_decoder();
	services.windows = &windows;
	services.canvases = &canvases;
	services.inputs = &inputs;
	services.clock = &clock;
	services.audio = &audio;
	CHECK(mh_video_playback_open(&services, &videos, NULL) ==
		MH_ERROR_NONE);
	memset(&options, 0, sizeof(options));
	options.resources = resources;
	options.reference = entry.path;
	options.mute = 0;

	CHECK(check_playback(&fixture, videos, &options, PLAY_TO_END) == 0);
	CHECK(check_playback(&fixture, videos, &options, SKIP_PLAYBACK) == 0);
	CHECK(check_playback(&fixture, videos, &options, CANCEL_PLAYBACK) == 0);

	CHECK(fixture.audio_opens == fixture.audio_closes &&
		fixture.starts == 3);
	CHECK(check_audio_failure(&fixture, videos, &options) == 0);
	services.audio = NULL;
	CHECK(check_silent_playback(&fixture, videos, &options) == 0);
	CHECK(check_open_failures(&fixture, videos, &options, &services) == 0);
	services.audio = &audio;
	CHECK(check_borrowed_window(&fixture, videos, &options, &windows) == 0);
	mh_video_playback_close(videos);
	mh_memory_resources_close(resources);
	mh_video_decoder_close(fixture.reference);
	free(data.data);
	services.audio = &audio;
	CHECK(check_alternate(&fixture, &services) == 0);
	puts("Movie services: real decoding, headless presentation, exact S16LE PCM, pending drain, host clock/events and teardown pass");

	return 0;
}
