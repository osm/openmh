#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"
#include "engine/ui/menu_ghost.h"

unsigned int
mh_menu_ghost_list_focus(const struct mh_menu_ghost_state *state)
{
	return state->patched ? MH_MENU_GHOST_FOCUS_PATCHED_RECORDINGS
			: MH_MENU_GHOST_FOCUS_RECORDINGS;
}

static int
valid_state(const struct mh_menu_ghost_state *state)
{
	return state != NULL &&
		(state->focus <= MH_MENU_GHOST_FOCUS_DEMO ||
			state->focus == mh_menu_ghost_list_focus(state) ||
			(state->patched &&
				state->focus ==
					MH_MENU_GHOST_FOCUS_BENCHMARK)) &&
		state->mode <= MH_GAME_DEMO &&
		state->count <= MH_MENU_DEMO_MAX_FILES &&
		(state->count == 0 ? state->selected == 0
				: state->selected < state->count) &&
		state->pending_delete <= 1 &&
		state->delete_answer <= MH_U32_MAX;
}

static void
emit_event(struct mh_menu_ghost_output *events, unsigned int kind,
	unsigned int value)
{
	events->events[events->count].kind = kind;

	events->events[events->count].value = value;
	events->count++;
}

static void
move_focus(struct mh_menu_ghost_state *state,
	const struct mh_menu_ghost_input *input,
	struct mh_menu_ghost_output *events)
{
	if (input->keys.left == 1) {
		if (state->focus == mh_menu_ghost_list_focus(state)) {
			state->focus = MH_MENU_GHOST_FOCUS_RACE;
			emit_event(events, MH_MENU_SOUND,
				MH_MENU_SOUND_VALUE_NEXT);
		} else
			emit_event(events, MH_MENU_SOUND,
				MH_MENU_SOUND_VALUE_LIMIT);
	}

	if (input->keys.right == 1) {
		if (state->focus != mh_menu_ghost_list_focus(state) &&
			state->count != 0) {
			state->focus = mh_menu_ghost_list_focus(state);
			emit_event(events, MH_MENU_SOUND,
				MH_MENU_SOUND_VALUE_PREVIOUS);
		} else
			emit_event(events, MH_MENU_SOUND,
				MH_MENU_SOUND_VALUE_LIMIT);
	}
}

static void
move_mode_focus(struct mh_menu_ghost_state *state,
	const struct mh_menu_ghost_input *input,
	struct mh_menu_ghost_output *events)
{
	if (input->keys.up == 1) {
		if (state->focus != MH_MENU_GHOST_FOCUS_RACE) {
			state->focus--;
			emit_event(events, MH_MENU_SOUND,
				MH_MENU_SOUND_ROW_PREVIOUS);
		} else
			emit_event(
				events, MH_MENU_SOUND, MH_MENU_SOUND_ROW_LIMIT);
	}

	if (input->keys.down == 1) {
		if (state->focus <
			(state->patched ? MH_MENU_GHOST_FOCUS_BENCHMARK
					: MH_MENU_GHOST_FOCUS_DEMO)) {
			state->focus++;
			emit_event(
				events, MH_MENU_SOUND, MH_MENU_SOUND_ROW_NEXT);
		} else
			emit_event(
				events, MH_MENU_SOUND, MH_MENU_SOUND_ROW_LIMIT);
	}
}

static void
select_recording(struct mh_menu_ghost_state *state,
	const struct mh_menu_ghost_input *input,
	struct mh_menu_ghost_output *events)
{
	if (input->keys.up == 1) {
		if (state->selected != 0) {
			state->selected--;
			emit_event(events, MH_MENU_SOUND,
				MH_MENU_SOUND_ROW_PREVIOUS);
			emit_event(events, MH_MENU_GHOST_NAME, state->selected);
		} else
			emit_event(
				events, MH_MENU_SOUND, MH_MENU_SOUND_ROW_LIMIT);
	}

	if (input->keys.down == 1) {
		if (state->selected + 1 < state->count) {
			state->selected++;
			emit_event(
				events, MH_MENU_SOUND, MH_MENU_SOUND_ROW_NEXT);
			emit_event(events, MH_MENU_GHOST_NAME, state->selected);
		} else
			emit_event(
				events, MH_MENU_SOUND, MH_MENU_SOUND_ROW_LIMIT);
	}
}

static void
request_delete(struct mh_menu_ghost_state *state,
	const struct mh_menu_ghost_input *input,
	struct mh_menu_ghost_output *events)
{
	if (input->remove == 1) {
		if (input->confirmation != 0) {
			state->delete_answer = 0;
			emit_event(events, MH_MENU_DIALOG,
				MH_MENU_CONFIRM_DEMO_DELETE);
		} else
			state->delete_answer = 1;
	}

	if (state->delete_answer == 1) {
		state->delete_answer = 0;
		emit_event(events, MH_MENU_GHOST_DELETE, state->selected);

		if (state->count == 1)
			state->selected = 0;
		else if (state->selected + 1 == state->count)
			state->selected--;
		else
			state->selected++;

		emit_event(events, MH_MENU_GHOST_NAME, state->selected);
		emit_event(events, MH_MENU_GHOST_REFRESH, 0);
		state->pending_delete = 1;
	}
}

static void
apply_enter_escape(struct mh_menu_ghost_state *state,
	const struct mh_menu_ghost_input *input,
	struct mh_menu_ghost_output *events)
{
	if (input->keys.enter == 1) {
		if (state->focus == mh_menu_ghost_list_focus(state)) {
			emit_event(
				events, MH_MENU_SOUND, MH_MENU_SOUND_CONFIRM);
			emit_event(events, MH_MENU_PAGE,
				state->mode == MH_GAME_GHOST ? MH_MENU_CAR_SETUP
							: MH_MENU_QUICK);
		} else if (state->count != 0) {
			state->focus = mh_menu_ghost_list_focus(state);
			emit_event(events, MH_MENU_SOUND,
				MH_MENU_SOUND_VALUE_PREVIOUS);
		} else
			emit_event(events, MH_MENU_SOUND,
				MH_MENU_SOUND_VALUE_LIMIT);
	}

	if (input->keys.escape == 1) {
		if (state->focus == mh_menu_ghost_list_focus(state)) {
			state->focus = MH_MENU_GHOST_FOCUS_RACE;
			emit_event(events, MH_MENU_SOUND,
				MH_MENU_SOUND_VALUE_NEXT);
		} else {
			emit_event(events, MH_MENU_SOUND, MH_MENU_SOUND_CANCEL);
			emit_event(events, MH_MENU_PAGE, MH_MENU_SINGLE);
		}
	}
}

enum mh_error_code
mh_menu_ghost_enter(
	struct mh_menu_ghost_state *state, struct mh_error *error_out)
{
	if (!valid_state(state))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Ghost menu entry");

	state->focus = MH_MENU_GHOST_FOCUS_RACE;
	state->delete_answer = 0;
	state->pending_delete = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_ghost_update(struct mh_menu_ghost_state *state,
	const struct mh_menu_ghost_input *input,
	struct mh_menu_ghost_output *output, struct mh_error *error_out)
{
	struct mh_menu_ghost_state next;
	struct mh_menu_ghost_output events;

	if (!valid_state(state) || state->pending_delete || input == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Ghost menu input");

	next = *state;
	memset(&events, 0, sizeof(events));

	if (next.count == 0 && next.focus == mh_menu_ghost_list_focus(&next))
		next.focus = MH_MENU_GHOST_FOCUS_RACE;

	move_focus(&next, input, &events);

	if (next.focus != mh_menu_ghost_list_focus(&next))
		move_mode_focus(&next, input, &events);
	else {
		select_recording(&next, input, &events);
		request_delete(&next, input, &events);
	}

	if (!next.pending_delete)
		apply_enter_escape(&next, input, &events);

	*state = next;
	*output = events;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_ghost_finish_delete(struct mh_menu_ghost_state *state,
	unsigned int count, unsigned int selected,
	const struct mh_menu_ghost_input *input,
	struct mh_menu_ghost_output *output, struct mh_error *error_out)
{
	struct mh_menu_ghost_state next;
	struct mh_menu_ghost_output events;

	if (!valid_state(state) || !state->pending_delete || input == NULL ||
		output == NULL || count > MH_MENU_DEMO_MAX_FILES ||
		(count == 0 ? selected != 0 : selected >= count))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Ghost menu refresh");

	next = *state;
	memset(&events, 0, sizeof(events));
	next.count = count;
	next.selected = selected;
	next.pending_delete = 0;

	if (count == 0)
		next.focus = MH_MENU_GHOST_FOCUS_RACE;
	else
		emit_event(&events, MH_MENU_GHOST_NAME, selected);

	apply_enter_escape(&next, input, &events);

	*state = next;
	*output = events;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
