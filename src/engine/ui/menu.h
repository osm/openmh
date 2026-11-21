#ifndef MH_MENU_H
#define MH_MENU_H

/*
 * Frontend page dispatch and transition requests. The application owns the
 * pages, hooks and context for the lifetime of the menu. Callbacks run
 * synchronously and may request another page or open a modal. Timers are
 * unsigned 32-bit milliseconds stored in unsigned long.
 */
struct mh_menu;
typedef void (*mh_menu_callback)(struct mh_menu *menu, void *context);

struct mh_menu_page {
	unsigned long		timer;
	unsigned int		layout;

	mh_menu_callback	init, draw, input, leave;
};

struct mh_menu_hooks {
	void			(*sound)(void *context, unsigned int event);
	void			(*input_mode)(void *context, unsigned int mode);

	mh_menu_callback	modal;
};

struct mh_menu {
	struct mh_menu_page		*current, *pending, *main;
	const struct mh_menu_hooks	*hooks;
	void				*context;
	unsigned long			elapsed_ms, duration_ms, idle_ms;
	unsigned int			leaving, modal, layout, position_mix;
	float				progress;
};

#define MH_MENU_EVENTS 8

/* Original UI sound request IDs, mapped to samples by menu audio. */
enum mh_menu_sound_request {
	MH_MENU_SOUND_CANCEL = 1,
	MH_MENU_SOUND_PAGE_IN = 2,
	MH_MENU_SOUND_PAGE_OUT = 3,
	MH_MENU_SOUND_PREVIOUS = 4,
	MH_MENU_SOUND_NEXT = 5,
	MH_MENU_SOUND_LIMIT = 6,
	MH_MENU_SOUND_ROW_PREVIOUS = 7,
	MH_MENU_SOUND_ROW_NEXT = 8,
	MH_MENU_SOUND_ROW_LIMIT = 9,
	MH_MENU_SOUND_CHANGE = 10,
	MH_MENU_SOUND_CONFIRM = 11,
	MH_MENU_SOUND_VALUE_PREVIOUS = 12,
	MH_MENU_SOUND_VALUE_NEXT = 13,
	MH_MENU_SOUND_BIND_START = 14,
	MH_MENU_SOUND_VALUE_LIMIT = 15,
	MH_MENU_SOUND_SLIDER_INCREASE = 16,
	MH_MENU_SOUND_SLIDER_DECREASE = 17,
	MH_MENU_SOUND_SLIDER_LIMIT = 18,
	MH_MENU_SOUND_TOGGLE_ON = 19,
	MH_MENU_SOUND_TOGGLE_OFF = 20,
	MH_MENU_SOUND_TOGGLE_LIMIT = 21,
	MH_MENU_SOUND_LAPS_NEXT = 22,
	MH_MENU_SOUND_LAPS_NEXT_LIMIT = 23,
	MH_MENU_SOUND_LAPS_PREVIOUS = 24,
	MH_MENU_SOUND_LAPS_PREVIOUS_LIMIT = 25,
	MH_MENU_SOUND_COLUMN_PREVIOUS = 26,
	MH_MENU_SOUND_COLUMN_NEXT = 27,
	MH_MENU_SOUND_COLUMN_LIMIT = 28,
	MH_MENU_SOUND_BIND_ACCEPT = 29,
	MH_MENU_SOUND_BIND_SWAP = 30,
	MH_MENU_SOUND_BIND_REJECT = 31,
	MH_MENU_SOUND_TEXT_ACCEPT = 34,
	MH_MENU_SOUND_TEXT_REJECT = 35,
	MH_MENU_SOUND_CAR_LOCKED = 36,
	MH_MENU_SOUND_TRACK_LOCKED = 37
};

enum mh_menu_destination {
	MH_MENU_SINGLE = 1,
	MH_MENU_MULTI,
	MH_MENU_RANKINGS,
	MH_MENU_OPTIONS,
	MH_MENU_CREDITS,
	MH_MENU_EXIT,
	MH_MENU_MAIN,
	MH_MENU_QUICK,
	MH_MENU_RACE_SETUP,
	MH_MENU_LEAGUE_SETUP,
	MH_MENU_GHOST_SETUP,
	MH_MENU_CAR_SETUP,
	MH_MENU_LEAGUE_STANDINGS,
	MH_MENU_LEAGUE_LOAD,
	MH_MENU_LEAGUE_NEW,
	MH_MENU_LEAGUE_DELETE,
	MH_MENU_PERSONAL,
	MH_MENU_GRAPHICS,
	MH_MENU_CONTROLS,
	MH_MENU_SOUND_OPTIONS
};

enum mh_menu_event_kind {
	MH_MENU_SOUND = 1,
	MH_MENU_PAGE,
	MH_MENU_DIALOG,
	MH_MENU_HORN
};

enum mh_menu_dialog_kind {
	MH_MENU_CONFIRM_EXIT = 1,
	MH_MENU_BLOCK_EXIT,
	MH_MENU_CONFIRM_DEMO_DELETE,
	MH_MENU_CONFIRM_RECORD_SAVE,
	MH_MENU_CONFIRM_RECORD_OVERWRITE,
	MH_MENU_CONFIRM_LEAGUE_CREATE,
	MH_MENU_CONFIRM_LEAGUE_DELETE,
	MH_MENU_CONFIRM_ADDRESS_DELETE
};

struct mh_menu_event {
	unsigned int	kind, value;
};

/* Shared navigation events: only value 1 fires. */
struct mh_menu_keys {
	unsigned int	left, right, up, down, enter, escape;
};

struct mh_menu_input {
	/* Event table results: only 1 fires. */
	unsigned int	left, right, enter, escape;

	/* CFG MenuConfirmation: Off=0, On=1, NoExit=2. */
	unsigned int	confirmation;
};

struct mh_menu_output {
	struct mh_menu_event	events[MH_MENU_EVENTS];
	unsigned int		count;
};

void	mh_menu_init(struct mh_menu *menu, struct mh_menu_page *main,
	const struct mh_menu_hooks *hooks, void *context);

/*
 * Select the entry page from the preceding race context, before its first
 * dispatch. Mode 1 returns to standings unless progression is Champion (4);
 * mode 4 returns to Multi. Other/absent contexts use Main. Pages must be bound.
 * Only current/layout change; page timers survive.
 */
void	mh_menu_restore_page(struct mh_menu *menu,
	struct mh_menu_page *standings, struct mh_menu_page *multi,
	int snapshot, unsigned int mode, unsigned int progression);
void	mh_menu_request(struct mh_menu *menu, struct mh_menu_page *page);

/*
 * Set elapsed_ms before stepping. No current page returns zero; otherwise
 * returns one. duration_ms must be nonzero and all timer inputs <=0xffffffff.
 * Callbacks must keep current valid until the dispatcher completes;
 * request() changes pending, not current.
 */
int	mh_menu_step(struct mh_menu *menu);

/* Also request a lightning flash for these error events. */
int	mh_menu_sound_flash(unsigned int event);

#endif
