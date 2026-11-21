#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu.h"
#include "engine/ui/menu_main.h"
#include "engine/ui/menu_modal.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "menu line %d: %s\n", __LINE__, #x);   \
			return 1;                                              \
		}                                                              \
	} while (0)

#define TRACE_CAPACITY 32

enum trace_event {
	TRACE_INIT = 10,
	TRACE_DRAW = 11,
	TRACE_INPUT = 12,
	TRACE_LEAVE = 13,
	TRACE_MODAL = 14
};

struct menu_trace {
	unsigned int		events[TRACE_CAPACITY], count;
	struct mh_menu_page	*next;
};

static void
record_event(void *context, unsigned int value)
{
	struct menu_trace *trace;

	trace = (struct menu_trace *)context;
	trace->events[trace->count++] = value;
}

static void
page_init(struct mh_menu *menu, void *context)
{
	(void)menu;
	record_event(context, TRACE_INIT);
}

static void
page_draw(struct mh_menu *menu, void *context)
{
	(void)menu;
	record_event(context, TRACE_DRAW);
}

static void
page_input(struct mh_menu *menu, void *context)
{
	struct menu_trace *trace;

	trace = (struct menu_trace *)context;
	record_event(context, TRACE_INPUT);
	mh_menu_request(menu, trace->next);
}

static void
page_leave(struct mh_menu *menu, void *context)
{
	(void)menu;
	record_event(context, TRACE_LEAVE);
}

static void
modal_input(struct mh_menu *menu, void *context)
{
	(void)menu;
	record_event(context, TRACE_MODAL);
}

static int
check_dispatch(void)
{
	static const struct mh_menu_hooks hooks = {
		record_event, NULL, modal_input};

	struct mh_menu menu;
	struct mh_menu_page first, next;
	struct menu_trace trace;

	memset(&first, 0, sizeof(first));
	memset(&next, 0, sizeof(next));
	memset(&trace, 0, sizeof(trace));
	first.init = page_init;
	first.draw = page_draw;
	first.input = page_input;
	first.leave = page_leave;
	first.layout = 7;
	next.init = page_init;
	next.layout = 9;
	trace.next = &next;
	mh_menu_init(&menu, &first, &hooks, &trace);
	CHECK(menu.duration_ms == 150 && menu.idle_ms == 30000);
	CHECK(mh_menu_step(&menu) == 1 && first.timer == 1);
	CHECK(trace.count == 2 && trace.events[0] == 2 &&
		trace.events[1] == TRACE_INIT);
	menu.elapsed_ms = 160;
	CHECK(mh_menu_step(&menu) == 1 && first.timer == 161 && menu.leaving);
	CHECK(menu.current == &first && menu.pending == &next &&
		menu.idle_ms == 1000000000UL);
	CHECK(trace.events[2] == TRACE_DRAW && trace.events[3] == TRACE_INPUT &&
		trace.events[4] == 3);
	CHECK(mh_menu_step(&menu) == 1 && first.timer == 1 &&
		menu.position_mix == 0);
	CHECK(mh_menu_step(&menu) == 1 && first.timer == 0 &&
		menu.position_mix == 256);
	CHECK(menu.current == &next && menu.layout == 7 && next.timer == 0 &&
		!menu.leaving);
	CHECK(trace.events[trace.count - 2] == TRACE_DRAW &&
		trace.events[trace.count - 1] == TRACE_LEAVE);
	CHECK(mh_menu_step(&menu) == 1 && next.timer == 160 &&
		menu.layout == 9);
	menu.modal = 1;
	CHECK(mh_menu_step(&menu) == 1 &&
		trace.events[trace.count - 1] == TRACE_MODAL);
	mh_menu_request(&menu, &first);
	CHECK(menu.idle_ms == 30000);
	menu.current = NULL;
	CHECK(mh_menu_step(&menu) == 0);

	return 0;
}

static int
check_main_input(void)
{
	struct mh_menu_main_state state, saved;
	struct mh_menu_input keys;
	struct mh_menu_output output, old;

	memset(&keys, 0, sizeof(keys));
	state.selected = 0;
	state.exit_answer = 0;
	keys.left = keys.right = keys.enter = keys.escape = 1;
	CHECK(mh_menu_main_update(&state, &keys, &output, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.selected == 1 && state.exit_answer == 0 &&
		output.count == 6);
	CHECK(output.events[0].value == 6 && output.events[1].value == 5);
	CHECK(output.events[3].kind == MH_MENU_PAGE &&
		output.events[3].value == MH_MENU_MULTI);
	CHECK(output.events[5].value == MH_MENU_EXIT);
	keys.confirmation = 1;
	state.exit_answer = 1;
	CHECK(mh_menu_main_update(&state, &keys, &output, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(output.count == 5 && output.events[4].kind == MH_MENU_DIALOG &&
		state.exit_answer == 0);
	keys.confirmation = 2;
	state.exit_answer = 1;
	CHECK(mh_menu_main_update(&state, &keys, &output, NULL, NULL) ==
			MH_ERROR_NONE &&
		output.count == 7);
	state.selected = 5;
	saved = state;
	memset(&output, 0xa5, sizeof(output));
	old = output;
	CHECK(mh_menu_main_update(&state, &keys, &output, NULL, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved, sizeof(state)) == 0 &&
		memcmp(&output, &old, sizeof(output)) == 0);

	return 0;
}

static int
check_modal_input(void)
{
	struct mh_menu_input keys;
	struct mh_menu_modal_state dialog;
	struct mh_menu_modal_output dialog_output;

	memset(&keys, 0, sizeof(keys));
	keys.enter = 1;
	CHECK(mh_menu_modal_open(&dialog, 2, 2, 2, NULL) == MH_ERROR_NONE &&
		dialog.selected == 1);
	CHECK(mh_menu_modal_step(&dialog, 79, &keys, &dialog_output, NULL) ==
		MH_ERROR_NONE);
	CHECK(dialog.x == 154.0f && dialog.answer == 0 &&
		dialog_output.display_x == -320.0f);
	keys.left = 1;
	CHECK(mh_menu_modal_step(&dialog, 1, &keys, &dialog_output, NULL) ==
		MH_ERROR_NONE);
	CHECK(dialog.x == 160.0f && dialog.answer == 1 &&
		dialog.direction == -1.0f);
	CHECK(dialog_output.display_selected == 1 && dialog_output.count == 3);
	CHECK(mh_menu_modal_step(&dialog, 80, &keys, &dialog_output, NULL) ==
			MH_ERROR_NONE &&
		dialog.visible == 1);
	CHECK(mh_menu_modal_step(&dialog, 1, &keys, &dialog_output, NULL) ==
			MH_ERROR_NONE &&
		dialog.visible == 0);
	CHECK(mh_menu_modal_open(&dialog, 2, 1, 2, NULL) == MH_ERROR_NONE);
	keys.escape = 1;
	CHECK(mh_menu_modal_step(&dialog, 80, &keys, &dialog_output, NULL) ==
		MH_ERROR_NONE);
	CHECK(dialog.answer == 2 && dialog_output.count == 5);

	return 0;
}

int
main(void)
{
	if (check_dispatch() || check_main_input() || check_modal_input())
		return 1;

	puts("menu: deferred page changes, transition endpoints, modal routing and main input passed");

	return 0;
}
