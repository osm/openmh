#include <stdlib.h>
#include <string.h>

#include "client/loading/view.h"
#include "client/session/loading.h"
#include "client/session/options.h"
#include "client/settings/render_choice.h"
#include "engine/catalog/load.h"
#include "engine/clock/game_timer.h"
#include "engine/render/viewport.h"
#include "engine/settings/config.h"
#include "engine/storage/record_file.h"
#include "interfaces/clock/clock.h"
#include "interfaces/render/backend.h"
#include "interfaces/storage/user_data.h"

static enum mh_error_code
complete_stage(void *context, enum mh_race_load_stage stage,
	struct mh_error *error_out)
{
	struct mh_session_loading *progress;
	unsigned long milliseconds;
	enum mh_error_code status;

	progress = context;

	if (progress == NULL || (unsigned int)stage != progress->next ||
		stage > MH_RACE_LOAD_FONT)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"out-of-order loading milestone");

	status = mh_clock_source_word(
		progress->options->timer->source, &milliseconds, error_out);

	if (status == MH_ERROR_NONE && progress->view != NULL)
		status = mh_loading_view_step(
			progress->view, milliseconds, error_out);

	/* The session releases the loading renderer on callback failure. */
	if (status != MH_ERROR_NONE)
		return mh_error_set(
			error_out, status, "cannot advance loading progress");

	if (progress->options->load_observer != NULL) {
		status = mh_race_load_complete(
			progress->options->load_observer, stage, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	progress->next++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_records(const struct mh_session_options *options, const char *track,
	unsigned int laps, struct mh_records *records,
	struct mh_error *error_out)
{
	char *directory;
	const char *path;
	enum mh_error_code status;

	memset(records, 0, sizeof(*records));

	if (options->no_records)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	directory = NULL;
	path = options->record_directory;
	status = MH_ERROR_NONE;

	if (path == NULL) {
		status = mh_user_data_directory(
			options->user_data, &directory, error_out);
		path = directory;
	}

	if (status == MH_ERROR_NONE)
		status = mh_record_file_load_from(options->user_data, path,
			track, laps, records, error_out);

	free(directory);

	return status;
}

static enum mh_error_code
open_view(const char *root, unsigned int track_number, unsigned int laps,
	struct mh_settings *settings, const struct mh_session_options *options,
	struct mh_loading_view **output, struct mh_error *error_out)
{
	struct mh_track track;
	struct mh_records records;
	struct mh_loading_layout layout;
	struct mh_loading_view *view;
	unsigned long milliseconds;
	enum mh_render_profile profile;
	enum mh_error_code status;

	if (settings == NULL || options == NULL || options->timer == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing loading session settings");

	status = mh_render_choice_profile(
		settings->renderer, &profile, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_render_choice_size(profile, &settings->av);
	status = mh_track_config_load_from(options->catalog_source,
		options->resources, root, track_number, &track, error_out);

	if (status == MH_ERROR_NONE)
		status = load_records(
			options, track.name, laps, &records, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_loading_layout(&track, track.name,
			settings->av.system,
			records.has_lap ? records.lap.milliseconds : 0,
			records.has_race ? records.race.milliseconds : 0,
			settings->av.width, settings->av.height, profile,
			&layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	view = NULL;
	status = mh_loading_view_open(options->font_source,
		options->image_decoder, options->resources, root,
		options->windows, options->window,
		mh_render_set_profile(options->renderers, profile),
		settings->av.width, settings->av.height,
		settings->av.fullscreen, &layout, &options->debug, &view,
		error_out);

	/* The first marker follows initial composition. */
	if (status == MH_ERROR_NONE)
		status = mh_clock_source_word(
			options->timer->source, &milliseconds, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_loading_view_step(view, milliseconds, error_out);

	if (status != MH_ERROR_NONE) {
		mh_loading_view_close(view);
		return mh_error_set(error_out, status,
			"cannot initialize loading progress");
	}

	*output = view;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_session_loading_init(struct mh_session_loading *loading, const char *root,
	const struct mh_session_options *options)
{
	memset(loading, 0, sizeof(*loading));

	loading->root = root;
	loading->options = options;
	loading->observer.context = loading;
	loading->observer.complete = complete_stage;
}

void
mh_session_loading_close(struct mh_session_loading *loading)
{
	mh_loading_view_close(loading->view);
	loading->view = NULL;
}

enum mh_error_code
mh_session_loading_begin(struct mh_session_loading *loading, unsigned int track,
	unsigned int laps, struct mh_settings *settings,
	struct mh_error *error_out)
{
	struct mh_settings defaults;

	if (loading == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (loading->view != NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"loading view is already open");

	/* Match the scene loader's no-settings viewport and default profile. */
	if (settings == NULL) {
		memset(&defaults, 0, sizeof(defaults));
		defaults.renderer = mh_render_choice_default();
		defaults.av.width = MH_VIEW_WIDTH;
		defaults.av.height = MH_VIEW_HEIGHT;
		settings = &defaults;
	}

	loading->next = 0;

	if (loading->options->single_control != NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return open_view(loading->root, track, laps, settings, loading->options,
		&loading->view, error_out);
}

const struct mh_race_load_observer *
mh_session_loading_observer(struct mh_session_loading *loading)
{
	return loading == NULL ? NULL : &loading->observer;
}

enum mh_error_code
mh_session_loading_stage(struct mh_session_loading *loading,
	enum mh_race_load_stage stage, struct mh_error *error_out)
{
	return mh_race_load_complete(
		mh_session_loading_observer(loading), stage, error_out);
}
