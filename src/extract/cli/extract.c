#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "engine/assets/packages.h"
#include "engine/core/text.h"
#include "extract/cli/extract.h"
#include "extract/output/compact.h"
#include "extract/output/package.h"
#include "extract/prepare/install.h"
#include "extract/source/input.h"

static volatile sig_atomic_t interrupted;

struct progress_display {
	mh_extract_progress	observer;
	void			*context;
	enum mh_extract_step	step;
	size_t			completed, total, shown;
	unsigned int		percent;
};

static void
stop_extraction(int number)
{
	(void)number;
	interrupted = 1;
}

static void
finish_progress(struct progress_display *display)
{
	if (display->total == 0 && display->completed != display->shown)
		fprintf(stderr, "  %lu items\n",
			(unsigned long)display->completed);
}

static const char *
step_title(enum mh_extract_step step)
{
	switch (step) {
	case MH_EXTRACT_UNPACK:
		return "Extracting game files";
	case MH_EXTRACT_READ_DISC:
		return "Reading disc files";
	case MH_EXTRACT_INSTALLER:
		return "Unpacking installer";
	case MH_EXTRACT_MOVIES:
		return "Extracting movies";
	case MH_EXTRACT_COPY:
		return "Copying files";
	case MH_EXTRACT_READ_PACKAGE:
		return "Reading package";
	case MH_EXTRACT_PATCH:
		return "Applying patch 3.0";
	case MH_EXTRACT_WRITE_PACKAGE:
		return "Writing package";
	default:
		return "Preparing content";
	}
}

static enum mh_error_code
show_progress(void *context, enum mh_extract_step step, const char *path,
	size_t completed, size_t total, struct mh_error *error_out)
{
	struct progress_display *display;
	unsigned int percent;

	if (interrupted)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "extraction interrupted");

	if (step == MH_EXTRACT_POLL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	display = context;

	if (display->observer != NULL)
		return display->observer(display->context, step, path,
			completed, total, error_out);

	if (step == MH_EXTRACT_NOTICE || step == MH_EXTRACT_CONVERT) {
		fprintf(stderr, "  %s%s\n",
			step == MH_EXTRACT_NOTICE ? "Note: " : "Converting: ",
			path);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	percent = total == 0
		? 0
		: (unsigned int)((double)completed * 100.0 / (double)total);

	if (step == MH_EXTRACT_DISC_FILE || step == MH_EXTRACT_AUDIO) {
		if (percent == 0 || percent == 50 || percent == 100)
			fprintf(stderr, "  %s %s: %u%%\n",
				step == MH_EXTRACT_AUDIO ? "CD audio track"
							: "Reading",
				path, percent);

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (step != display->step || total != display->total ||
		completed < display->completed) {
		finish_progress(display);
		fprintf(stderr, "%s...\n", step_title(step));
		memset(display, 0, sizeof(*display));
		display->step = step;
		display->percent = 101;
	}

	display->completed = completed;
	display->total = total;

	if (total != 0 &&
		(display->percent == 101 || percent == 100 ||
			percent / 25 > display->percent / 25)) {
		if (percent != display->percent)
			fprintf(stderr, "  %3u%% (%lu/%lu items)\n", percent,
				(unsigned long)completed, (unsigned long)total);

		display->percent = percent;
	} else if (total == 0 && completed - display->shown >= 500) {
		fprintf(stderr, "  %lu items\n", (unsigned long)completed);
		display->shown = completed;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_output(const char *const *sources, size_t count,
	const char *destination, const struct mh_extract_options *options,
	int *patched, struct progress_display *display,
	struct mh_error *error_out)
{
	size_t length;
	int package;
	struct mh_extract_input input;
	enum mh_error_code status;

	length = strlen(destination);
	package = length >= (sizeof(MH_ASSET_PACKAGE_EXTENSION) - 1) &&
		mh_ascii_equal(destination + length -
				(sizeof(MH_ASSET_PACKAGE_EXTENSION) - 1),
			MH_ASSET_PACKAGE_EXTENSION);

	if (options->keep_original && package)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"--keep-original requires directory output, not an .ass package");

	if (options->compact && !package)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"--compact requires an .ass package destination");

	if ((options->exclude_music || options->exclude_movies) && !package)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"--exclude-music and --exclude-movies require an .ass package destination");

	if (options->compact) {
		status = mh_extract_compact_available(options, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_extract_input_identify(sources[0], &input, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (count == 1 && input.kind == MH_EXTRACT_INPUT_GAME && package) {
		status = mh_extract_pack(sources[0], destination, options,
			show_progress, display, error_out);

		if (status == MH_ERROR_NONE)
			*patched = 0;

		return status;
	}

	return mh_extract_install(&input, sources + 1, count - 1, destination,
		package, options, show_progress, display, patched, error_out);
}

enum mh_error_code
mh_extract_run(const char *const *sources, size_t count,
	const char *destination, const struct mh_extract_options *options,
	int *patched, mh_extract_progress progress, void *context,
	struct mh_error *error_out)
{
	void (*previous_interrupt)(int), (*previous_terminate)(int);
	enum mh_error_code status;
	struct progress_display display;
	size_t i;

	if (sources == NULL || count == 0 || destination == NULL ||
		*destination == 0 || options == NULL || patched == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid extraction arguments");

	for (i = 0; i < count; i++) {
		if (sources[i] == NULL || *sources[i] == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing extraction source");
	}

	memset(&display, 0, sizeof(display));
	display.observer = progress;
	display.context = context;
	interrupted = 0;
	previous_interrupt = signal(SIGINT, stop_extraction);
	previous_terminate = signal(SIGTERM, stop_extraction);
	status = prepare_output(sources, count, destination, options, patched,
		&display, error_out);
	finish_progress(&display);

	if (previous_interrupt != SIG_ERR)
		signal(SIGINT, previous_interrupt);

	if (previous_terminate != SIG_ERR)
		signal(SIGTERM, previous_terminate);

	return status;
}
