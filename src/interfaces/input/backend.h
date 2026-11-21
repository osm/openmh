#ifndef MH_INPUT_BACKEND_H
#define MH_INPUT_BACKEND_H

#include "interfaces/input/force.h"
#include "interfaces/input/input.h"

struct mh_force_ops {
	enum mh_error_code (*poll)(void *context, void *input,
		const struct mh_force_effect initial[MH_FORCE_EFFECTS],
		struct mh_force_status	*output, struct mh_error *error_out);
	enum mh_error_code (*apply)(void *context, void *input,
		const struct mh_force_effect effects[MH_FORCE_EFFECTS],
		unsigned int changed, unsigned int start,
		struct mh_error		*error_out);
	void				(*stop)(void *context, void *input);
};

/*
 * Borrowed, immutable descriptor/table/context. The host keeps them and the
 * window system alive until all input owners close. Calls run on the event
 * thread. A provider owns one event stream; only the active screen drains it.
 * Hosts may enqueue events from callbacks: event() never waits for new input.
 * Lifecycle notifications are delivered to the host even when no screen is
 * polling; background/resume events must not depend on this queue draining.
 * Diagnostics remain borrowed until the next operation on this provider.
 */
struct mh_input_provider {
	const struct mh_input_ops	*ops;
	void				*context;
};

struct mh_input_ops {
	/* Failed open cleans partial state and leaves output unchanged. */
	enum mh_error_code (*open)(void *context, struct mh_window *window,
		int	grab_mouse, void **output, struct mh_error *error_out);
	void				(*close)(void *context, void *input);

	int	(*focused)(void *context, const void *input);

	/*
	 * input may be NULL for screens without device sampling. Return one
	 * event or zero for an empty queue, leaving output unchanged when
	 * empty. Update the supplied input before publishing the translated
	 * event.
	 */
	int	(*event)(void *context, void *input, struct mh_event *output);

	/*
	 * Poll uses portable key synthesis; failed polls leave snapshot
	 * unchanged.
	 */
	enum mh_error_code (*poll)(void *context, void *input,
		struct mh_keyboard *keyboard,
		const unsigned char down[MH_KEY_COUNT], unsigned long now_ms,
		unsigned long mode, struct mh_device_input *snapshot,
		struct mh_error		*error_out);

	/*
	 * Original scan identifiers and byte text mapping, not native key
	 * codes. A provider may synthesize these from a controller or touch UI.
	 * Text is the original single-byte repertoire, indexed by the same scan
	 * as its keyboard event; native composition/IME state stays with the
	 * host. Unicode editing is not part of the game contract.
	 */
	int	(*key_down)(void *context, unsigned int scan);
	void (*characters)(void *context,
		const unsigned char down[MH_KEY_COUNT],
		unsigned char		characters[MH_KEY_COUNT]);

	/*
	 * Optional; no output capability when absent. All three entries
	 * required.
	 */
	const struct mh_force_ops	*force;

	/* Change pointer ownership without reopening other input devices. */
	enum mh_error_code (*mouse_grab)(void *context, void *input,
		int			enabled, struct mh_error *error_out);
};

/* Initialization performs no I/O; invalid tables leave provider unchanged. */
enum mh_error_code	mh_input_provider_init(
	struct mh_input_provider *provider, const struct mh_input_ops *ops,
	void *context, struct mh_error *error_out);

#endif
