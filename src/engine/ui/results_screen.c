#include <stddef.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"
#include "engine/ui/results.h"

#define FINISH_ANCHOR_OFFSET_MS 25000
#define FINISH_DEADLINE_MS 35000UL

int
mh_results_board_required(unsigned int mode, int aborted)
{
	return mode == MH_GAME_LEAGUE ||
		(mode == MH_GAME_QUICK_RACE && !aborted);
}

static enum mh_error_code
prepare_board(struct mh_results_screen *screen, const struct mh_field *field,
	int aborted, unsigned long *random, struct mh_error *error_out)
{
	struct mh_result_estimate rows[MH_RACE_MAX_CARS];
	struct mh_standings next;
	unsigned long seed, winner, gap;
	unsigned int i;
	enum mh_error_code status;

	if (screen == NULL || field == NULL || random == NULL ||
		field->network || (!aborted && !screen->finish_seen))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid prepared results context");

	if (screen->prepared)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	seed = *random;
	status = mh_field_result_estimates_abort(
		field, aborted, &seed, rows, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = field->standings;
	winner = 0;

	for (i = 0; i < next.count; i++)
		if (rows[i].active && rows[i].position == 1)
			winner = rows[i].total_ms;

	for (i = 0; i < next.count; i++)
		if (rows[i].active) {
			if (aborted) {
				next.cars[i].position = rows[i].position;

				/*
				 * Playback has no original ordinary results
				 * board. Retain its valid presentation order
				 * for a native storage-error fallback: literal
				 * abort ranks after a departure need not be
				 * unique.
				 */
				if (!field->playback)
					next.order[rows[i].position - 1] = i;
			}

			next.cars[i].best_ms = rows[i].best_ms;
			next.cars[i].estimated = !rows[i].finished;
			next.cars[i].finish_ms = rows[i].total_ms;
			gap = (rows[i].total_ms - winner) & MH_U32_MAX;
			next.cars[i].gap_ms = gap <= (unsigned long)MH_I32_MAX
				? (long)gap
				: -1L - (long)(MH_U32_MAX - gap);
		}

	screen->final = next;
	screen->prepared = 1;
	screen->aborted = aborted;
	*random = seed;

	if (aborted)
		screen->finish_seen = screen->visible = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_results_screen_prepare(struct mh_results_screen *screen,
	const struct mh_field *field, unsigned long *random,
	struct mh_error *error_out)
{
	return prepare_board(screen, field, 0, random, error_out);
}

enum mh_error_code
mh_results_screen_abort(struct mh_results_screen *screen,
	const struct mh_field *field, unsigned long *random,
	struct mh_error *error_out)
{
	return prepare_board(screen, field, 1, random, error_out);
}

void
mh_results_screen_update(
	struct mh_results_screen *screen, const struct mh_standings *standings)
{
	if (standings->count != 0 && standings->cars[0].finished &&
		!screen->finish_seen) {
		screen->finish_seen = 1;
		screen->visible = 1;
	}
}

enum mh_error_code
mh_results_screen_update_local(struct mh_results_screen *screen,
	const struct mh_standings *standings, int local_active,
	long double now_ms, struct mh_error *error_out)
{
	struct mh_results_screen next;
	long double adjusted;

	if (screen == NULL || standings == NULL ||
		standings->count > MH_RESULTS_MAX_PLAYERS ||
		(local_active != 0 && local_active != 1) ||
		!(now_ms >= 0 && now_ms <= (long double)MH_U32_MAX) ||
		screen->finish_anchor_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid local finish clock");

	next = *screen;
	next.finish_clock_ms = now_ms;

	if (!next.finish_seen) {
		if (local_active && standings->count != 0 &&
			standings->cars[0].finished &&
			next.finish_anchor_ms == 0) {
			adjusted = now_ms - FINISH_ANCHOR_OFFSET_MS;
			next.finish_anchor_ms = adjusted < 0
				? (0UL - (unsigned long)(-adjusted)) &
					MH_U32_MAX
				: (unsigned long)adjusted;
		}

		if (next.finish_anchor_ms != 0 &&
			now_ms > ((next.finish_anchor_ms + FINISH_DEADLINE_MS) &
					MH_U32_MAX)) {
			next.finish_seen = 1;
			next.visible = 1;
		}
	}

	*screen = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_results_screen_frame(struct mh_results_screen *screen,
	struct mh_field *field, long double elapsed, struct mh_error *error_out)
{
	struct mh_drive_frame frame;
	struct mh_results_screen next;
	enum mh_error_code status;

	if (screen == NULL || field == NULL || field->network ||
		field->count < 1 || field->count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid local finish frame");

	status = mh_drive_frame_sample(
		&field->cars[0], elapsed, &frame, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *screen;
	status = mh_results_screen_update_local(&next, &field->standings,
		(field->active_mask & 1) != 0,
		(long double)frame.seconds * 1000.0L, error_out);

	if (status == MH_ERROR_NONE && !screen->finish_seen && next.finish_seen)
		status = mh_field_clock_advance(field, elapsed, error_out);

	if (status == MH_ERROR_NONE)
		*screen = next;

	return status;
}

int
mh_results_screen_pauses(const struct mh_results_screen *screen,
	const struct mh_standings *standings)
{
	return screen->visible &&
		(screen->prepared || standings->count == 0 ||
			!standings->cars[0].finished || standings->complete);
}
