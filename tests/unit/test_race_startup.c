#include <stdio.h>
#include <string.h>

#include "client/loading/view.h"
#include "client/race/internal.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "race startup:%d: %s\n", __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

/*
 * Exercise startup decisions without loading assets or starting native devices.
 */
struct mh_loading_view {
	int	references;
};
static struct mh_race_loop loop;
static struct mh_field field;
static struct mh_loading_view view;
static enum mh_loading_action action;
static unsigned int starts;
static int early_failure, clock_failure, draw_failure, quit;

int
mh_race_initialize(struct mh_race_loop *race)
{
	starts++;

	if (early_failure)
		return 0;

	race->success = race->running = !clock_failure;

	return 1;
}

enum mh_error_code
mh_race_loop_error(const struct mh_race_loop *race, struct mh_error *error_out)
{
	return mh_error_forward(error_out, &race->error);
}

enum mh_error_code
mh_loading_view_ready(struct mh_loading_view *loading,
	const struct mh_input_provider *inputs, struct mh_error *error_out)
{
	(void)loading;

	(void)inputs;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_loading_view_retain(struct mh_loading_view *loading)
{
	loading->references++;
}

void
mh_loading_view_close(struct mh_loading_view *loading)
{
	loading->references--;
}

enum mh_error_code
mh_loading_view_wait(struct mh_loading_view *loading,
	enum mh_loading_action *output, struct mh_error *error_out)
{
	(void)loading;

	*output = action;

	return mh_error_set(error_out,
		draw_failure ? MH_ERROR_BACKEND : MH_ERROR_NONE,
		draw_failure ? "draw failed" : NULL);
}

static void
reset(void)
{
	memset(&loop, 0, sizeof(loop));
	memset(&field, 0, sizeof(field));
	loop.field = &field;
	loop.loading = &view;
	loop.quit = &quit;
	view.references = 1;
	action = MH_LOADING_WAIT;
	starts = 0;
	early_failure = clock_failure = draw_failure = quit = 0;
}

static int
open_confirmation(void)
{
	CHECK(mh_race_startup_open(&loop, 1, NULL) == MH_ERROR_NONE);
	CHECK(starts == 0 && view.references == 2 && !loop.initialized);

	/* The preparing session releases its ownership after open. */
	mh_loading_view_close(&view);
	CHECK(view.references == 1);
	CHECK(mh_race_startup_step(&loop) == MH_APP_RUNNING);
	CHECK(starts == 0 && loop.delay > 0);

	return 0;
}

static int
check_continue(void)
{
	reset();
	CHECK(open_confirmation() == 0);
	action = MH_LOADING_CONTINUE;
	CHECK(mh_race_startup_step(&loop) == MH_APP_RUNNING);
	CHECK(starts == 1 && loop.initialized && loop.success);
	CHECK(view.references == 0 && loop.loading_wait == NULL &&
		loop.loading == NULL);

	return 0;
}

static int
check_close(void)
{
	reset();
	CHECK(open_confirmation() == 0);
	action = MH_LOADING_QUIT;
	CHECK(mh_race_startup_step(&loop) == MH_APP_DONE);
	CHECK(quit && starts == 0 && loop.success && !loop.initialized);
	CHECK(view.references == 0 && loop.loading_wait == NULL);

	return 0;
}

static int
check_failures(void)
{
	reset();
	CHECK(open_confirmation() == 0);
	draw_failure = 1;
	CHECK(mh_race_startup_step(&loop) == MH_APP_FAILED);
	CHECK(starts == 0 && view.references == 0 && !loop.initialized);
	reset();
	CHECK(open_confirmation() == 0);
	action = MH_LOADING_CONTINUE;
	early_failure = 1;
	CHECK(mh_race_startup_step(&loop) == MH_APP_FAILED);
	CHECK(starts == 1 && view.references == 0 && !loop.initialized);
	reset();
	CHECK(open_confirmation() == 0);
	action = MH_LOADING_CONTINUE;
	clock_failure = 1;
	CHECK(mh_race_startup_step(&loop) == MH_APP_FAILED);

	/*
	 * Late failures still own initialized race resources for close to
	 * release.
	 */
	CHECK(starts == 1 && loop.initialized && !loop.success &&
		!loop.running);
	CHECK(view.references == 0);

	return 0;
}

static int
check_automatic_starts(void)
{
	unsigned int i;

	for (i = 0; i < 6; i++) {
		reset();

		switch (i) {
		case 0:
			field.network = 1;
			break;

		case 1:
			field.attract = 1;
			break;

		case 2:
			loop.capture = "capture.tga";
			break;

		case 3:
			loop.headless = 1;
			break;

		case 4:
			loop.debug.skip_loading_wait = 1;
			break;

		case 5:
			break;
		}

		CHECK(mh_race_startup_open(&loop, i != 5, NULL) ==
			MH_ERROR_NONE);
		CHECK(starts == 1 && loop.initialized &&
			loop.loading_wait == NULL);
		CHECK(view.references == 1);
	}

	return 0;
}

int
main(void)
{
	CHECK(check_continue() == 0);
	CHECK(check_close() == 0);
	CHECK(check_failures() == 0);
	CHECK(check_automatic_starts() == 0);
	puts("Race startup waits without initialization, preserves failures and bypasses automatic sessions");

	return 0;
}
