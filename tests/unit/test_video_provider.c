#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "engine/assets/divi.h"
#include "engine/assets/divi_data.h"
#include "engine/core/binary.h"
#include "interfaces/video/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "movie provider line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct pending_player {
	const struct mh_video_options	*options;
	int				ready;
};

struct fixture {
	struct pending_player	*pending;
	unsigned int		opens, steps, closes;
	int			deny, fail;
	enum mh_video_outcome	outcome;
	char			diagnostic[80];
	unsigned char		payload[4];
};

static enum mh_error_code
open_player(void *context, const struct mh_video_options *options,
	void **output, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct pending_player *player;

	fixture = context;

	if (fixture->deny)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"movie service unavailable");

	player = calloc(1, sizeof(*player));

	if (player == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate test movie");

	player->options = options;
	fixture->pending = player;
	fixture->opens++;
	*output = player;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_video_status
step_player(void *context, void *token, unsigned long *delay,
	enum mh_video_outcome *outcome, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct pending_player *player;
	struct mh_file data;

	fixture = context;
	player = token;
	fixture->steps++;

	if (fixture->fail) {
		mh_error_set(error_out, MH_ERROR_BACKEND, fixture->diagnostic);
		return MH_VIDEO_FAILED;
	}

	if (!player->ready) {
		*delay = 2;
		mh_error_set(error_out, MH_ERROR_NONE, NULL);
		return MH_VIDEO_RUNNING;
	}

	if (mh_video_divi_read(player->options->resources,
		player->options->reference, player->options->index, &data,
		error_out) != MH_ERROR_NONE)
		return MH_VIDEO_FAILED;

	if (data.size == sizeof(fixture->payload))
		memcpy(fixture->payload, data.data, data.size);

	free(data.data);
	*outcome = fixture->outcome;
	*delay = 99; /* Terminal dispatch clears stale pacing hints. */

	return MH_VIDEO_DONE;
}

static void
close_player(void *context, void *token)
{
	struct fixture *fixture;

	fixture = context;
	fixture->pending = NULL;
	fixture->closes++;
	free(token);
}

static void
host_ready(struct fixture *fixture)
{
	/*
	 * Close detaches the pending callback target before releasing its
	 * token.
	 */
	if (fixture->pending != NULL)
		fixture->pending->ready = 1;
}

int
main(void)
{
	static const struct mh_video_ops ops = {
		open_player, step_player, close_player, NULL};

	struct mh_video_provider first, second, invalid;
	struct mh_video_ops incomplete;
	struct mh_video_options options;
	struct mh_video_player *a, *b;
	struct fixture f, g;
	struct mh_resource_entry entry;
	const struct mh_resource_source *resources;
	struct mh_file data, saved;
	struct mh_error error;
	unsigned char archive[MH_VIDEO_DIVI_HEADER_SIZE + 8];
	int outcome_a, outcome_b;
	unsigned int steps, i;

	memset(archive, 0, sizeof(archive));
	memcpy(archive, "DIVI", 4);
	mh_write_u32le(archive + 4, 2);
	mh_write_u32le(archive + 8, MH_VIDEO_DIVI_HEADER_SIZE);
	mh_write_u32le(archive + 16, MH_VIDEO_DIVI_HEADER_SIZE);
	mh_write_u32le(archive + 28, MH_VIDEO_DIVI_HEADER_SIZE + 4);
	memcpy(archive + MH_VIDEO_DIVI_HEADER_SIZE, "ABCD1234", 8);
	entry.path = "movies.pak";
	entry.data = archive;
	entry.size = sizeof(archive);
	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	data.data = NULL;
	data.size = 0;
	CHECK(mh_video_divi_read(resources, "movies.pak", 1, &data, NULL) ==
		MH_ERROR_NONE);
	CHECK(data.size == 4 && memcmp(data.data, "1234", 4) == 0);
	saved = data;
	CHECK(mh_video_divi_read(resources, "movies.pak", 2, &data, NULL) ==
		MH_ERROR_INVALID);
	CHECK(data.data == saved.data && data.size == saved.size);
	CHECK(mh_video_divi_read(resources, "missing", 0, &data, NULL) !=
		MH_ERROR_NONE);
	CHECK(data.data == saved.data && data.size == saved.size);
	free(data.data);
	memset(&f, 0, sizeof(f));
	memset(&g, 0, sizeof(g));
	f.outcome = MH_VIDEO_ENDED;
	g.outcome = MH_VIDEO_SKIPPED;
	CHECK(mh_video_provider_init(&first, &ops, &f, NULL) == MH_ERROR_NONE);
	CHECK(mh_video_provider_init(&second, &ops, &g, NULL) == MH_ERROR_NONE);
	incomplete = ops;
	incomplete.close = NULL;
	memset(&invalid, 0, sizeof(invalid));
	CHECK(mh_video_provider_init(&invalid, &incomplete, &f, NULL) ==
		MH_ERROR_INVALID);
	CHECK(invalid.ops == NULL);
	memset(&options, 0, sizeof(options));
	options.resources = resources;
	options.reference = "movies.pak";
	options.collection = 1;
	a = NULL;
	outcome_a = 99;
	CHECK(mh_video_open_from(NULL, &options, &outcome_a, &a, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(a == NULL && outcome_a == MH_VIDEO_NO_OUTCOME);
	f.deny = 1;
	CHECK(mh_video_open_from(&first, &options, &outcome_a, &a, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(a == NULL && f.opens == 0 && f.closes == 0);
	f.deny = 0;
	CHECK(mh_video_open_from(&first, &options, &outcome_a, &a, NULL) ==
		MH_ERROR_NONE);
	options.index = 1;
	CHECK(mh_video_open_from(&second, &options, &outcome_b, &b, NULL) ==
		MH_ERROR_NONE);
	options.reference = "caller changed options";
	CHECK(mh_video_step(a) == MH_VIDEO_RUNNING && mh_video_delay(a) == 2);
	CHECK(mh_video_step(b) == MH_VIDEO_RUNNING && mh_video_delay(b) == 2);
	host_ready(&g);
	CHECK(mh_video_step(b) == MH_VIDEO_DONE && mh_video_delay(b) == 0);
	CHECK(outcome_b == MH_VIDEO_NO_OUTCOME &&
		memcmp(g.payload, "1234", 4) == 0);
	steps = g.steps;
	CHECK(mh_video_step(b) == MH_VIDEO_DONE && g.steps == steps);
	CHECK(mh_video_close(b) && outcome_b == MH_VIDEO_SKIPPED);
	CHECK(f.pending != NULL && g.pending == NULL);
	host_ready(&f);
	CHECK(mh_video_step(a) == MH_VIDEO_DONE &&
		memcmp(f.payload, "ABCD", 4) == 0);
	CHECK(mh_video_close(a) && outcome_a == MH_VIDEO_ENDED);
	options.reference = "movies.pak";

	/*
	 * Cancel both immediately and while waiting; late readiness does
	 * nothing.
	 */
	for (i = 0; i < 2; i++) {
		CHECK(mh_video_open_from(&first, &options, &outcome_a, &a,
			NULL) == MH_ERROR_NONE);

		if (i != 0)
			CHECK(mh_video_step(a) == MH_VIDEO_RUNNING);

		CHECK(!mh_video_close(a) && outcome_a == MH_VIDEO_NO_OUTCOME);
		host_ready(&f);
		CHECK(f.pending == NULL);
	}

	f.fail = 1;
	strcpy(f.diagnostic, "delayed decoder failure");
	CHECK(mh_video_open_from(&first, &options, &outcome_a, &a, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_video_step(a) == MH_VIDEO_FAILED && mh_video_delay(a) == 0);
	strcpy(f.diagnostic, "overwritten provider message");
	CHECK(mh_video_error(a, &error) == MH_ERROR_BACKEND);
	CHECK(strcmp(error.message, "delayed decoder failure") == 0);
	steps = f.steps;
	CHECK(mh_video_step(a) == MH_VIDEO_FAILED && f.steps == steps);
	CHECK(!mh_video_close(a) && outcome_a == MH_VIDEO_NO_OUTCOME);
	CHECK(f.opens == f.closes && g.opens == g.closes);
	mh_memory_resources_close(resources);
	puts("movie providers: memory archives, pending readiness, outcomes and cancellation pass");

	return 0;
}
