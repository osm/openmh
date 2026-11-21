#include <string.h>

#include "client/race/console.h"
#include "client/race/internal.h"
#include "engine/game/mode.h"
#include "engine/window/title.h"

static void
format_race_time(char *text, unsigned long milliseconds)
{
	sprintf(text, "%lu:%02lu.%03lu", milliseconds / 60000,
		(milliseconds / 1000) % 60, milliseconds % 1000);
}

void
mh_race_print_results(FILE *file, const struct mh_field *field)
{
	const struct mh_race_result *result;
	unsigned int i, entry, active;
	char total[32], gap[32], best[32];

	if (file == NULL || field->standings.count == 0)
		return;

	active = field->count;

	if (field->network || field->playback) {
		active = 0;

		for (i = 0; i < field->count; i++)
			if (field->active_mask & (1U << i))
				active++;
	}

	fprintf(file, "\n%s (%u/%u finished)\n",
		field->standings.complete ? "Race results" : "Race standings",
		field->standings.finished_count, active);
	fputs("Pos  Car               Status          Total       Gap (s)      Best lap\n",
		file);

	for (i = 0; i < field->standings.count; i++) {
		entry = field->standings.order[i];
		result = &field->standings.cars[entry];
		strcpy(total, "--");
		strcpy(gap, "--");
		strcpy(best, "--");

		if (result->finished) {
			format_race_time(total, result->finish_ms);
			sprintf(gap, "%+.3f", result->gap_ms / 1000.0);
		}

		if (result->best_lap != 0)
			format_race_time(best, result->best_ms);

		fprintf(file, "%2u%s  %-16.16s  %-8s  %12s  %12s  %12s\n",
			result->position, entry == 0 ? "*" : " ",
			field->cars[entry].car->name,
			(field->network || field->playback) &&
					!(field->active_mask & (1U << entry))
				? "LEFT"
				: result->finished ? "FINISHED"
						: "RACING",
			total, gap, best);
	}

	fputs("* controlled car\n", file);
	fflush(file);
}

static const char *race_start_title[] = {
	" | READY", " | 3", " | 2", " | 1", " | GO!", ""};

static const char *
results_title(const struct mh_race_loop *loop)
{
	if (loop->screen.prepared)
		return " | Race results";

	if (loop->field->standings.complete)
		return " | RESULTS";

	if (loop->drive->race.finished)
		return " | FINISHED";

	return "";
}

static const char *
mode_title(const struct mh_race_loop *loop)
{
	if (loop->field->playback == MH_GAME_DEMO)
		return " | DEMO";

	if (loop->field->playback == MH_GAME_GHOST)
		return " | GHOST";

	if (loop->continuous)
		return " | NETWORK";

	if (loop->field->time_attack)
		return " | TIME ATTACK";

	if (loop->drive->ai_path != NULL)
		return " | AUTONOMOUS";

	return "";
}

static void
format_debug_title(const struct mh_race_loop *loop, char *title)
{
	char race_title[192];

	race_title[0] = 0;

	if (loop->drive->race.lap_limit != 0) {
		sprintf(race_title,
			" | Pos %u/%u | Lap %u/%u | %.2fs | Best %.2fs%s%s",
			loop->screen.prepared
				? loop->screen.final.cars[0].position
				: loop->field->standings.cars[0].position,
			loop->field->count, loop->drive->race.lap,
			loop->drive->race.lap_limit,
			(loop->drive->race.finished ? loop->drive->race.last_ms
						: loop->drive->race.now_ms -
						loop->drive->race
							.lap_start_ms) /
				1000.0,
			loop->drive->race.best_lap == 0
				? 0
				: loop->drive->race.best_ms / 1000.0,
			results_title(loop),
			loop->drive->race.wrong_way ? " | WRONG WAY" : "");
	}

	sprintf(title, "Motorhead%s | %u cars | %u %s | %s%u | %.0f RPM%s%s%s",
		mode_title(loop), loop->field->count,
		mh_hud_display_speed((float)loop->drive->vehicle.speed,
			loop->context->settings == NULL
				? MH_UNITS_METRIC
				: loop->context->settings->system),
		loop->context->settings != NULL &&
				loop->context->settings->system ==
					MH_UNITS_IMPERIAL
			? "mph"
			: "km/h",
		loop->drive->vehicle.engine.gear == 0 ? "R" : "G",
		loop->drive->vehicle.engine.gear,
		loop->drive->vehicle.engine.rpm, race_title,
		race_start_title[mh_drive_start_phase(loop->drive)],
		loop->paused
			? (loop->continuous ? " | INPUT RELEASED" : " | PAUSED")
			: "");

	if (loop->field->attract)
		strcpy(title,
			"Motorhead | DEMO | Enter or Escape returns to menu");
}

void
mh_race_update_window_title(struct mh_race_loop *loop)
{
	char title[512];

	loop->was_results = loop->screen.visible;

	if (mh_window_diagnostic_titles(loop->window))
		format_debug_title(loop, title);
	else
		sprintf(title, "Motorhead - %.63s",
			loop->context->scene.assets.track.name);

	(void)mh_window_set_title(loop->window, title, NULL);
}
