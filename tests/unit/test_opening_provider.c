#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "client/application/opening.h"
#include "engine/storage/asset_path.h"
#include "interfaces/video/backend.h"
#include "interfaces/window/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "opening provider line %d: %s\n",      \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct opening_fixture {
	const struct mh_video_options	*pending;
	const struct mh_resource_source	*resources;
	struct mh_window		*window;
	unsigned int			windows;
	unsigned int	acquired, released, opens, closes, index;
	int				ready, bad, deny;
	enum mh_video_outcome		outcome;
};

static enum mh_error_code
acquire(void *context, const struct mh_window_system_options *options,
	struct mh_error *error_out)
{
	struct opening_fixture *fixture;

	(void)options;
	fixture = context;
	fixture->acquired++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
release(void *context)
{
	struct opening_fixture *fixture;

	fixture = context;

	if (fixture->pending != NULL)
		fixture->bad = 1;

	fixture->released++;
}

static enum mh_error_code
open_window(void *context, const char *title, unsigned int width,
	unsigned int height, unsigned int flags, void **output,
	struct mh_error *error_out)
{
	(void)title;

	(void)width;
	(void)height;
	(void)flags;
	((struct opening_fixture *)context)->windows++;
	*output = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
destroy_window(void *context, void *window)
{
	(void)window;
	((struct opening_fixture *)context)->windows--;
}

static unsigned long
window_id(void *context, const void *window)
{
	(void)context;
	(void)window;

	return 0;
}

static enum mh_error_code
open_video(void *context, const struct mh_video_options *options, void **output,
	struct mh_error *error_out)
{
	struct opening_fixture *fixture;

	fixture = context;
	fixture->opens++;

	if (fixture->deny)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"fixture movie unavailable");

	fixture->pending = options;
	fixture->index = options->index;
	fixture->ready = 0;
	*output = fixture;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_video_status
step_video(void *context, void *token, unsigned long *delay,
	enum mh_video_outcome *outcome, struct mh_error *error_out)
{
	struct opening_fixture *fixture;
	struct mh_file bytes;

	fixture = context;

	if (token != fixture || fixture->pending == NULL ||
		fixture->pending->resources != fixture->resources ||
		fixture->pending->window != fixture->window ||
		fixture->pending->index != fixture->index ||
		!fixture->pending->collection || !fixture->pending->mute ||
		fixture->pending->capture != NULL ||
		fixture->acquired != fixture->released + 1)
		fixture->bad = 1;

	if (!fixture->ready) {
		*delay = 7;
		return MH_VIDEO_RUNNING;
	}

	/* Read only after readiness: the owner's path must still be live. */
	if (mh_resource_source_read(fixture->pending->resources,
		fixture->pending->reference, &bytes,
		error_out) != MH_ERROR_NONE)
		return MH_VIDEO_FAILED;

	if (bytes.size != 4 || memcmp(bytes.data, "test", 4) != 0)
		fixture->bad = 1;

	free(bytes.data);
	*outcome = fixture->outcome;

	return MH_VIDEO_DONE;
}

static void
close_video(void *context, void *token)
{
	struct opening_fixture *fixture;

	(void)token;
	fixture = context;
	fixture->pending = NULL;
	fixture->closes++;
}

static enum mh_error_code
resolve_video(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, char *output, size_t capacity,
	int *found, struct mh_error *error_out)
{
	struct opening_fixture *fixture;

	fixture = context;

	if (strstr(reference, ".pak") == NULL)
		fixture->bad = 1;

	/* The original collection does not exist in this converted content. */

	return mh_asset_path_find_from(resources, root, "Converted/Intro.clip",
		output, capacity, found, error_out);
}

static int
check_sequence_window(enum mh_video_outcome outcome, int expected,
	unsigned int count, int cancel, int deny, const char *edition_id,
	int borrowed)
{
	static const struct mh_video_ops video_ops = {
		open_video, step_video, close_video, resolve_video};

	static const struct mh_window_ops window_ops = {acquire, release,
		open_window, destroy_window, window_id, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL};

	static const struct mh_resource_entry entries[] = {
		{"disc/Converted/Intro.clip", (const unsigned char *)"test",
			4}};

	struct mh_video_provider videos;
	struct mh_window_system windows;
	struct opening_fixture fixture;
	struct mh_activity activity;
	const struct mh_content_edition *edition;
	unsigned int i;
	int result;

	memset(&fixture, 0, sizeof(fixture));
	fixture.outcome = outcome;
	fixture.deny = deny;
	edition = mh_content_edition_find(edition_id);
	CHECK(edition != NULL);
	CHECK(mh_memory_resources_open(entries, 1, &fixture.resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_video_provider_init(&videos, &video_ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_window_system_init(&windows, &window_ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	mh_activity_init(&activity);

	/* No video capability must not enumerate this nonexistent root. */
	CHECK(mh_opening_begin(NULL, fixture.resources, &windows, NULL,
		"missing", 1, 0, &edition->intro, &result, &activity,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_activity_step(&activity) == MH_APP_DONE);
	CHECK(result == MH_OPENING_ENDED && fixture.acquired == 0);
	mh_activity_close(&activity);

	if (borrowed) {
		CHECK(mh_window_system_acquire(&windows, NULL, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_window_open(&windows, "Application", 960, 600, 0,
			&fixture.window, NULL) == MH_ERROR_NONE);
	}

	CHECK(mh_opening_begin(&videos, fixture.resources, &windows,
		fixture.window, "disc", 1, 1, &edition->intro, &result,
		&activity, NULL) == MH_ERROR_NONE);
	CHECK(fixture.acquired == (borrowed || edition->intro.count != 0) &&
		fixture.opens == 0);

	if (cancel) {
		CHECK(mh_activity_step(&activity) == MH_APP_RUNNING);
		CHECK(fixture.pending != NULL &&
			mh_activity_delay(&activity) == 7);
	} else {
		for (i = 0; i < count; i++) {
			if (!deny) {
				CHECK(mh_activity_step(&activity) ==
					MH_APP_RUNNING);
				CHECK(fixture.index == i &&
					mh_activity_delay(&activity) == 7);
				fixture.ready = 1;
			}

			CHECK(mh_activity_step(&activity) ==
				(i + 1 == count ? MH_APP_DONE
						: MH_APP_RUNNING));
		}

		CHECK(result == expected && fixture.opens == count);
		CHECK(mh_activity_step(&activity) == MH_APP_DONE);
	}

	mh_activity_close(&activity);
	CHECK(fixture.pending == NULL && !fixture.bad);
	CHECK(fixture.windows == (unsigned int)borrowed);
	CHECK(fixture.acquired == fixture.released + borrowed);

	if (borrowed) {
		mh_window_destroy(fixture.window);
		mh_window_system_release(&windows);
	}

	CHECK(fixture.acquired == fixture.released && fixture.windows == 0);
	CHECK(fixture.closes == (deny ? 0 : fixture.opens));
	mh_memory_resources_close(fixture.resources);

	return 0;
}

static int
check_sequence(enum mh_video_outcome outcome, int expected, unsigned int count,
	int cancel, int deny, const char *edition_id)
{
	CHECK(check_sequence_window(outcome, expected, count, cancel, deny,
		edition_id, 0) == 0);

	CHECK(check_sequence_window(outcome, expected, count, cancel, deny,
		edition_id, 1) == 0);

	return 0;
}

int
main(void)
{
	static const char *const ids[] = {"retail-europe",
		"motordemo_soft_intro", "motordemo_3dfx_intro",
		"motordemo2_3dfx_intro"};

	static const unsigned int counts[] = {3, 2, 2, 4};
	unsigned int i;

	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		CHECK(check_sequence(MH_VIDEO_ENDED, MH_OPENING_ENDED,
			counts[i], 0, 0, ids[i]) == 0);
		CHECK(check_sequence(MH_VIDEO_SKIPPED, MH_OPENING_SKIPPED, 1, 0,
			0, ids[i]) == 0);
		CHECK(check_sequence(MH_VIDEO_CLOSED, MH_OPENING_CLOSED, 1, 0,
			0, ids[i]) == 0);
		CHECK(check_sequence(MH_VIDEO_ENDED, MH_OPENING_ENDED,
			counts[i], 0, 1, ids[i]) == 0);
		CHECK(check_sequence(MH_VIDEO_ENDED, MH_OPENING_FAILED, 1, 1, 0,
			ids[i]) == 0);
	}

	CHECK(check_sequence(MH_VIDEO_ENDED, MH_OPENING_ENDED, 0, 0, 0,
		"motordemo_soft") == 0);
	CHECK(check_sequence(MH_VIDEO_ENDED, MH_OPENING_ENDED, 0, 0, 0,
		"motordemo2_soft") == 0);
	CHECK(check_sequence(MH_VIDEO_ENDED, MH_OPENING_ENDED, 0, 0, 0,
		"motordemo_3dfx") == 0);
	CHECK(check_sequence(MH_VIDEO_ENDED, MH_OPENING_ENDED, 0, 0, 0,
		"motordemo_mplayer") == 0);
	puts("opening providers: memory paths, pending playback, sequence outcomes and teardown pass");

	return 0;
}
