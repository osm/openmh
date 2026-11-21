#ifndef MH_INTERFACES_INPUT_INPUT_H
#define MH_INTERFACES_INPUT_INPUT_H

#include "engine/core/error.h"
#include "engine/input/devices.h"

struct mh_window;
struct mh_input_backend;
struct mh_input_provider;

/* Zero initialize before opening; close is safe for closed/failed objects. */
struct mh_input {
	struct mh_input_backend	*backend;
};

enum mh_event_type {
	MH_EVENT_OTHER,
	MH_EVENT_QUIT,
	MH_EVENT_WINDOW_CLOSE,
	MH_EVENT_FOCUS_LOST,
	MH_EVENT_FOCUS_GAINED,
	MH_EVENT_KEY_DOWN,
	MH_EVENT_KEY_UP,
	MH_EVENT_MOUSE_MOTION,
	MH_EVENT_MOUSE_WHEEL,
	/*
	 * Discard held keys, synthesized repeats and device deltas after
	 * resume.
	 */
	MH_EVENT_INPUT_RESET
};

/* Logical shortcuts, distinct from original physical scan indices. */
enum mh_shortcut {
	MH_SHORTCUT_NONE,
	MH_SHORTCUT_ESCAPE,
	MH_SHORTCUT_ENTER,
	MH_SHORTCUT_TAB,
	MH_SHORTCUT_SPACE,
	MH_SHORTCUT_HOME,
	MH_SHORTCUT_F1,
	MH_SHORTCUT_F2,
	MH_SHORTCUT_F3,
	MH_SHORTCUT_F4,
	MH_SHORTCUT_E,
	MH_SHORTCUT_F,
	MH_SHORTCUT_G,
	MH_SHORTCUT_M,
	MH_SHORTCUT_N,
	MH_SHORTCUT_P,
	MH_SHORTCUT_Q,
	MH_SHORTCUT_R
};

/*
 * Mouse motion is relative window-coordinate motion (not drawable pixels or
 * an absolute touch location). Wheel motion is in provider-normalized steps.
 * Hosts translate absolute pointers through their presentation transform before
 * synthesizing game actions; native pointer IDs/coordinates stay in the host.
 */
struct mh_event {
	enum mh_event_type	type;
	unsigned long		window;
	unsigned int		scan;
	enum mh_shortcut	shortcut;
	int			repeat, shift;
	float			mouse_x, mouse_y, wheel_y;
	int			mouse_left;
};

/*
 * Borrow the provider and window until close. NULL window creates inert device
 * sampling for deterministic captures; events still come from the provider.
 * grab_mouse requests hidden, unbounded relative input while focused. Zero
 * leaves the pointer visible and free to leave the window; mouse deltas and
 * buttons remain available inside it. Providers without a pointer may ignore
 * the request. The application chooses when fullscreen requires a grab.
 */
enum mh_error_code	mh_input_open_from(
	const struct mh_input_provider *provider, struct mh_input *input,
	struct mh_window *window, int grab_mouse, struct mh_error *error_out);
enum mh_error_code	mh_input_set_mouse_grab(struct mh_input *input,
	int enabled, struct mh_error *error_out);

/*
 * Nonblocking queue drains, with or without an owned device sampler. Return one
 * event (including OTHER) or zero when empty, leaving output unchanged. An
 * owned sampler is updated before returning its event. No OS event data
 * escapes.
 */
int	mh_input_event(struct mh_input *input, struct mh_event *event);
int	mh_input_provider_event(const struct mh_input_provider *provider,
	struct mh_event *event);
int	mh_input_key_down(const struct mh_input_provider *provider,
	unsigned int scan);
void	mh_input_characters(const struct mh_input_provider *provider,
	const unsigned char down[MH_KEY_COUNT],
	unsigned char characters[MH_KEY_COUNT]);

void			mh_input_close(struct mh_input *input);
int			mh_input_focused(const struct mh_input *input);
enum mh_error_code	mh_input_poll(struct mh_input *input,
	struct mh_keyboard *keyboard, const unsigned char down[MH_KEY_COUNT],
	unsigned long now_ms, unsigned long mode,
	struct mh_device_input *snapshot, struct mh_error *error_out);

#endif
