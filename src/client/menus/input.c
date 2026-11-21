#include "client/menus/internal.h"
#include "interfaces/input/input.h"

void
mh_menus_set_input(struct mh_menus *menus, const struct mh_keyboard *keyboard,
	const unsigned char *down)
{
	menus->navigation_keys.left = keyboard->events[MH_KEY_LEFT];

	menus->navigation_keys.right = keyboard->events[MH_KEY_RIGHT];
	menus->navigation_keys.enter = keyboard->events[MH_KEY_ENTER];
	menus->navigation_keys.escape = keyboard->events[MH_KEY_ESCAPE];

	menus->credits_keys.escape = menus->navigation_keys.escape;
	menus->credits_keys.enter = menus->navigation_keys.enter;
	menus->credits_keys.control = keyboard->held[MH_KEY_LCTRL];
	menus->credits_keys.left_alt = keyboard->held[MH_KEY_LALT];
	menus->credits_keys.right_alt = keyboard->held[MH_KEY_RALT];

	menus->setup_keys.left = menus->navigation_keys.left;
	menus->setup_keys.right = menus->navigation_keys.right;
	menus->setup_keys.enter = menus->navigation_keys.enter;
	menus->setup_keys.escape = menus->navigation_keys.escape;
	menus->setup_keys.up = keyboard->events[MH_KEY_UP];
	menus->setup_keys.down = keyboard->events[MH_KEY_DOWN];

	menus->rankings_keys.escape = menus->navigation_keys.escape;
	menus->rankings_keys.left = menus->navigation_keys.left;
	menus->rankings_keys.right = menus->navigation_keys.right;
	menus->rankings_keys.up = menus->setup_keys.up;
	menus->rankings_keys.down = menus->setup_keys.down;

	menus->sound_keys.up = menus->setup_keys.up;
	menus->sound_keys.down = menus->setup_keys.down;
	menus->sound_keys.left = menus->navigation_keys.left;
	menus->sound_keys.right = menus->navigation_keys.right;
	menus->sound_keys.enter = menus->navigation_keys.enter;
	menus->sound_keys.escape = menus->navigation_keys.escape;

	menus->controls_keys.held_left = keyboard->held[MH_KEY_LEFT];
	menus->controls_keys.held_right = keyboard->held[MH_KEY_RIGHT];
	menus->controls_keys.shift = keyboard->held[MH_KEY_LSHIFT];
	menus->controls_keys.control = keyboard->held[MH_KEY_LCTRL];
	menus->controls_keys.alt = keyboard->held[MH_KEY_LALT];
	menus->controls_keys.left = menus->navigation_keys.left;
	menus->controls_keys.right = menus->navigation_keys.right;
	menus->controls_keys.up = menus->setup_keys.up;
	menus->controls_keys.down = menus->setup_keys.down;
	menus->controls_keys.enter = menus->navigation_keys.enter;
	menus->controls_keys.escape = menus->navigation_keys.escape;
	menus->controls_keys.held_count = keyboard->held_count;
	menus->controls_keys.first_held =
		keyboard->held_count != 0 ? keyboard->held_keys[0] : 0;

	menus->sound_keys.held_left = keyboard->held[MH_KEY_LEFT];
	menus->sound_keys.held_right = keyboard->held[MH_KEY_RIGHT];
	menus->sound_keys.shift = keyboard->held[MH_KEY_LSHIFT];
	menus->sound_keys.control = keyboard->held[MH_KEY_LCTRL];
	menus->sound_keys.alt = keyboard->held[MH_KEY_LALT];

	menus->graphics_keys.up = menus->setup_keys.up;
	menus->graphics_keys.down = menus->setup_keys.down;
	menus->graphics_keys.left = menus->navigation_keys.left;
	menus->graphics_keys.right = menus->navigation_keys.right;
	menus->graphics_keys.escape = menus->navigation_keys.escape;
	menus->graphics_keys.held_left = keyboard->held[MH_KEY_LEFT];
	menus->graphics_keys.held_right = keyboard->held[MH_KEY_RIGHT];
	menus->graphics_keys.shift = keyboard->held[MH_KEY_LSHIFT];
	menus->graphics_keys.control = keyboard->held[MH_KEY_LCTRL];
	menus->graphics_keys.alt = keyboard->held[MH_KEY_LALT];

	menus->ghost_input.keys = menus->setup_keys;
	menus->ghost_input.remove = keyboard->events[MH_KEY_DELETE];
	menus->ghost_input.confirmation = menus->navigation_keys.confirmation;

	memcpy(menus->saving_input.keys, keyboard->events,
		sizeof(keyboard->events));
	mh_input_characters(
		menus->inputs, down, menus->saving_input.characters);

	memcpy(menus->graphics_keys.keys, keyboard->events,
		sizeof(keyboard->events));
	memcpy(menus->graphics_keys.characters, menus->saving_input.characters,
		sizeof(menus->graphics_keys.characters));

	memcpy(menus->personal_keys.keys, keyboard->events,
		sizeof(keyboard->events));
	memcpy(menus->personal_keys.characters, menus->saving_input.characters,
		sizeof(menus->personal_keys.characters));

	memcpy(menus->league_input.keys, keyboard->events,
		sizeof(keyboard->events));
	memcpy(menus->league_input.characters, menus->saving_input.characters,
		sizeof(menus->league_input.characters));

	memcpy(menus->multi_keys.keys, keyboard->events,
		sizeof(keyboard->events));
	memcpy(menus->multi_keys.characters, menus->saving_input.characters,
		sizeof(menus->multi_keys.characters));
}
