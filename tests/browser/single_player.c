/*
 * Link wrappers belong only to the browser acceptance executable. The ordinary
 * application owns menus, loading, simulation, results and persistent saves.
 * The AI supplies driving skill; it does not skip laps or manufacture results.
 */
#include "client/application/application_internal.h"
#include "client/menus/internal.h"
#include "client/race/internal.h"
#include "engine/game/mode.h"
#include "interfaces/video/decoder.h"
#include "league.h"

void			mh_browser_flow_movie(unsigned int frames,
	double interval);
enum mh_error_code	__real_mh_video_decoder_read(
	struct mh_video_decoder *decoder, struct mh_video_frame *output,
	int *available, struct mh_error *error_out);

enum mh_error_code
__wrap_mh_video_decoder_read(struct mh_video_decoder *decoder,
	struct mh_video_frame *output, int *available,
	struct mh_error *error_out)
{
	static unsigned int frames;
	enum mh_error_code status;

	status = __real_mh_video_decoder_read(
		decoder, output, available, error_out);

	if (status == MH_ERROR_NONE && *available) {
		frames++;
		mh_browser_flow_movie(
			frames, mh_video_decoder_info(decoder)->interval_us);
	}

	return status;
}

void			mh_browser_flow_menu(const char *page,
	unsigned int selected, unsigned int network, int transition,
	unsigned int commits, unsigned int recording, unsigned int modal);
void			mh_browser_flow_race(const char *stage,
	unsigned int lap, int finished, unsigned long milliseconds,
	unsigned int cars, int complete, unsigned int frames,
	unsigned int commits, double x, double z, unsigned int held, int paused,
	unsigned int position, unsigned int music_track);
void			mh_browser_flow_suspended(int suspended,
	unsigned long ticks);
enum mh_error_code	__real_mh_application_set_suspended(
	struct mh_application *application, int suspended,
	struct mh_error *error_out);

enum mh_error_code
__wrap_mh_application_set_suspended(struct mh_application *application,
	int suspended, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = __real_mh_application_set_suspended(
		application, suspended, error_out);

	if (status == MH_ERROR_NONE)
		mh_browser_flow_suspended(application->suspended,
			mh_clock_source_ticks(application->timer.source));

	return status;
}

int			__real_mh_menu_step(struct mh_menu *menu);
enum mh_error_code	__real_mh_race_loop_open(
	struct mh_race_context *context, struct mh_field *field,
	const struct mh_race_loop_options *options,
	struct mh_race_loop **output, struct mh_error *error_out);
enum mh_app_status	__real_mh_race_loop_step(struct mh_race_loop *loop);
int			__real_mh_race_loop_close(struct mh_race_loop *loop);

static struct mh_field *active_field;
static struct mh_race_results_handler original_results, observed_results;
static unsigned int commits, race_frames;

int	mh_browser_flow_manual_drive(void);
void	mh_browser_flow_input(double throttle, double steering, long mouse_x,
	long mouse_y);
void	mh_browser_flow_selection(unsigned int single, unsigned int mode,
	unsigned int focus, unsigned int count, unsigned int selected,
	const char *name);
void	mh_browser_flow_race_setup(unsigned int mode, unsigned int laps,
	int remote, unsigned int track);

static const char *
page_name(const struct mh_menus *menus)
{
	const struct mh_menu_page *page;

	page = menus->menu.current;

	if (page == &menus->main_page)
		return "main";

	if (page == &menus->single_page)
		return "single";

	if (page == &menus->race_page)
		return "setup";

	if (page == &menus->car_page)
		return "car";

	if (page == &menus->multi_page)
		return "multiplayer";

	if (page == &menus->save_page)
		return "recording";

	if (page == &menus->ghost_page)
		return "ghost";

	if (page == &menus->league_page[MH_MENUS_LEAGUE_SETUP])
		return "league";

	if (page == &menus->league_page[MH_MENUS_LEAGUE_STANDINGS])
		return "standings";

	return "other";
}

int
__wrap_mh_menu_step(struct mh_menu *menu)
{
	const struct mh_menus *menus;
	int result;

	result = __real_mh_menu_step(menu);
	menus = (const struct mh_menus *)menu->context;
	mh_browser_flow_menu(page_name(menus), menus->main.selected,
		menus->configuration.items[MH_MENU_FEATURE_NETWORK],
		menu->leaving != 0 ||
			(menu->current != NULL &&
				menu->current->timer < menu->duration_ms),
		commits, menus->saving.phase, menu->modal);
	mh_browser_flow_selection(menus->single.selected, menus->ghost.mode,
		menus->ghost.focus, menus->ghost.count, menus->ghost.selected,
		menus->demo_name);
	mh_browser_flow_league_menu(menus);

	return result;
}

static enum mh_error_code
accept_results(void *context, const struct mh_standings *standings,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	(void)context;

	if (active_field == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"acceptance results have no active race");

	status = original_results.commit(
		original_results.context, standings, error_out);

	if (status == MH_ERROR_NONE)
		commits++;

	return status;
}

static enum mh_error_code
prepare_driver(struct mh_race_context *context, struct mh_field *field,
	const char *root, unsigned int difficulty, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (mh_browser_flow_manual_drive() || field->cars[0].remote)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/* Ghostmode needs no AI path until the fixture supplies its driver. */
	status = MH_ERROR_NONE;

	if (context->scene.assets.ai_path.points == NULL)
		status = mh_race_scene_load_ai_from(
			context->resources, root, &context->scene, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_drive_enable_ai(&field->cars[0],
		&context->scene.assets.ai_path, difficulty, error_out);
}

static unsigned int
race_mode(const struct mh_field *field,
	const struct mh_race_loop_options *options)
{
	if (field->playback)
		return (unsigned int)field->playback;

	if (options->session != NULL && options->session->league)
		return MH_GAME_LEAGUE;

	return field->time_attack ? MH_GAME_TIME_ATTACK : MH_GAME_QUICK_RACE;
}

enum mh_error_code
__wrap_mh_race_loop_open(struct mh_race_context *context,
	struct mh_field *field, const struct mh_race_loop_options *options,
	struct mh_race_loop **output, struct mh_error *error_out)
{
	struct mh_race_loop_options observed;
	enum mh_error_code status;

	status = prepare_driver(context, field, options->root,
		race_mode(field, options) == MH_GAME_LEAGUE ? 3 : 2, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	active_field = field;
	race_frames = 0;
	mh_browser_flow_race_setup(race_mode(field, options),
		field->cars[0].race.lap_limit, field->cars[0].remote,
		context->scene.assets.track_number);
	observed = *options;

	if (options->result_handler != NULL) {
		original_results = *options->result_handler;
		observed_results.context = NULL;
		observed_results.commit = accept_results;
		observed.result_handler = &observed_results;
	}

	return __real_mh_race_loop_open(
		context, field, &observed, output, error_out);
}

enum mh_app_status
__wrap_mh_race_loop_step(struct mh_race_loop *loop)
{
	enum mh_app_status status;
	const struct mh_drive *drive;
	const char *stage;
	unsigned int held, i;

	status = __real_mh_race_loop_step(loop);
	drive = &active_field->cars[0];

	if (!loop->initialized)
		stage = "loading";
	else if (loop->screen.visible && loop->results_ready)
		stage = "results";
	else if (drive->race.finished)
		stage = "finish";
	else
		stage = "race";

	race_frames++;
	held = 0;

	for (i = 0; i < MH_KEY_COUNT; i++)
		if (loop->down[i] != 0)
			held++;

	mh_browser_flow_race(stage, drive->race.lap, drive->race.finished,
		drive->race.now_ms, active_field->count,
		active_field->standings.complete, race_frames, commits,
		drive->vehicle.motion.position[0],
		drive->vehicle.motion.position[2], held,
		loop->paused || loop->menu.visible,
		active_field->standings.cars[0].position,
		loop->context->scene.assets.track.cd_track);
	mh_browser_flow_input(loop->controls.throttle, loop->controls.steering,
		loop->device_input.mouse[0], loop->device_input.mouse[1]);

	return status;
}

int
__wrap_mh_race_loop_close(struct mh_race_loop *loop)
{
	int result;

	result = __real_mh_race_loop_close(loop);
	active_field = NULL;

	return result;
}
