#include <math.h>
#include <string.h>

#include "client/session/playback_hud.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"

#define MESSAGE_COLOR 0xe06820UL

enum {
	MESSAGE_MARGIN = 10,
	MESSAGE_MAX_WIDTH = 540,
	PLAYBACK_STATUS_FLAGS = MH_HUD_STATUS_GHOST | MH_HUD_STATUS_RECORDING |
		MH_HUD_STATUS_DEMO
};

/* Order also breaks ties between notices at the same clock sample. */
enum notice_index {
	PLAYBACK_STARTED,
	PLAYBACK_ENDED,
	RECORDING_STARTED,
	RECORDING_FAILED,
	NOTICE_COUNT
};

static double
drive_clock_ms(const struct mh_drive *drive)
{
	return drive->frame.active
		? (double)(unsigned long)((long double)drive->frame.seconds *
			1000.0L)
		: floor(drive->ticks * (1000.0 / 120));
}

void
mh_playback_hud_init(struct mh_playback_hud *hud)
{
	memset(hud, 0, sizeof(*hud));
	mh_messages_init(&hud->messages);
}

void
mh_playback_hud_restart(
	struct mh_playback_hud *hud, const struct mh_field *field)
{
	/*
	 * Sampled replay clocks now retain lifetime time across restart, just
	 * like local races. Only legacy diagnostic clocks need translation.
	 */
	hud->clock_offset_ms = field->cars[0].frame.active
		? 0
		: hud->clock_ms - drive_clock_ms(&field->cars[0]);

	hud->clock_ms = hud->clock_offset_ms + drive_clock_ms(&field->cars[0]);
	hud->last_ticks = field->cars[0].ticks;
	hud->delivered_notices = 0;
}

static unsigned int
collect_notices(const struct mh_demo_race *race,
	const struct mh_demo_recording *recording, unsigned int mode,
	double times[NOTICE_COUNT], const char *text[NOTICE_COUNT])
{
	unsigned int pending;

	pending = 0;
	memset(times, 0, sizeof(*times) * NOTICE_COUNT);
	text[PLAYBACK_STARTED] = mode == MH_GAME_DEMO ? "Demo play started!"
						: "Ghost play started!";
	text[PLAYBACK_ENDED] = mode == MH_GAME_DEMO ? "End of demo play!"
						: "End of ghost play!";
	text[RECORDING_STARTED] = "Race recording started!";
	text[RECORDING_FAILED] = "Error! Could not write demo file!";

	if (race != NULL && race->playback.anchor_ms != 0) {
		pending |= 1U << PLAYBACK_STARTED;
		times[PLAYBACK_STARTED] = race->playback.anchor_ms;
	}

	if (race != NULL && race->notice == MH_DEMO_NOTICE_ENDED) {
		pending |= 1U << PLAYBACK_ENDED;
		times[PLAYBACK_ENDED] = race->notice_ms;
	}

	if (mh_demo_recording_start_notice(recording)) {
		pending |= 1U << RECORDING_STARTED;
		times[RECORDING_STARTED] =
			mh_demo_recording_start_ms(recording);
	}

	if (mh_demo_recording_failure_notice(recording)) {
		pending |= 1U << RECORDING_FAILED;
		times[RECORDING_FAILED] =
			mh_demo_recording_failure_ms(recording);
	}

	return pending;
}

static enum mh_error_code
deliver_notices(struct mh_playback_hud *hud, const struct mh_hud_assets *assets,
	unsigned int pending, const double times[NOTICE_COUNT],
	const char *const text[NOTICE_COUNT], struct mh_error *error_out)
{
	unsigned int i, chosen;
	double stamp;
	enum mh_error_code status;

	while (pending != 0) {
		chosen = NOTICE_COUNT;

		for (i = 0; i < NOTICE_COUNT; i++)
			if ((pending & (1U << i)) &&
				(chosen == NOTICE_COUNT ||
					times[i] < times[chosen]))
				chosen = i;

		stamp = hud->clock_offset_ms + times[chosen];

		if (!(stamp >= 0 && stamp <= hud->clock_ms))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"playback HUD notice clock out of range");

		status = mh_messages_add(&hud->messages, text[chosen], NULL,
			MESSAGE_COLOR, (unsigned long)stamp,
			assets->message_advance, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		hud->delivered_notices |= 1U << chosen;
		pending &= ~(1U << chosen);
		hud->has_messages = 1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_playback_hud_frame(struct mh_playback_hud *hud,
	const struct mh_hud_assets *assets, unsigned int width,
	const struct mh_field *field, const struct mh_demo_race *race,
	const struct mh_demo_recording *recording, struct mh_error *error_out)
{
	struct mh_playback_hud next;
	const char *text[NOTICE_COUNT];
	enum mh_error_code status;
	double times[NOTICE_COUNT], now;
	unsigned int pending, mode;

	if (hud == NULL || assets == NULL || field == NULL ||
		field->count == 0 || width < MH_HUD_MIN_WIDTH ||
		width > MH_HUD_VIEW_LIMIT ||
		field->cars[0].ticks < hud->last_ticks ||
		(race != NULL && field != &race->field))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid playback HUD context");

	next = *hud;
	next.last_ticks = field->cars[0].ticks;
	now = drive_clock_ms(&field->cars[0]);
	next.clock_ms = next.clock_offset_ms + now;

	if (!(next.clock_ms >= 0 && next.clock_ms <= (double)MH_U32_MAX))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"playback HUD clock out of range");

	mode = race == NULL ? 0 : race->field.playback;
	next.status_flags = mh_hud_status_flags(0, mode,
		race != NULL && race->playback.active,
		mh_demo_recording_active(recording));
	status = mh_messages_width(&next.messages,
		width - MESSAGE_MARGIN < MESSAGE_MAX_WIDTH
			? width - MESSAGE_MARGIN
			: MESSAGE_MAX_WIDTH,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	pending = collect_notices(race, recording, mode, times, text);
	pending &= ~next.delivered_notices;

	status =
		deliver_notices(&next, assets, pending, times, text, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*hud = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_playback_hud_state(
	const struct mh_playback_hud *hud, struct mh_hud_state *state)
{
	state->status_flags = (state->status_flags & MH_U32_MAX &
				~(unsigned long)PLAYBACK_STATUS_FLAGS) |
		hud->status_flags;

	state->messages = hud->has_messages ? &hud->messages : NULL;
	state->message_ms = hud->clock_ms;
}

enum mh_error_code
mh_playback_hud_message(struct mh_playback_hud *hud,
	const struct mh_hud_assets *assets, const char *text,
	const char *suffix, unsigned long color, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (hud == NULL || assets == NULL ||
		!(hud->clock_ms >= 0 && hud->clock_ms <= (double)MH_U32_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid playback message clock");

	status = mh_messages_add(&hud->messages, text, suffix, color,
		(unsigned long)hud->clock_ms, assets->message_advance,
		error_out);

	if (status == MH_ERROR_NONE)
		hud->has_messages = 1;

	return status;
}
