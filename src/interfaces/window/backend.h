#ifndef MH_WINDOW_BACKEND_H
#define MH_WINDOW_BACKEND_H

#include "interfaces/window/window.h"

/*
 * The host owns the descriptor, operation table and context until all acquired
 * window-system references and windows are released. Calls run on the host's
 * event thread; descriptors and callbacks remain unchanged. Acquire/release own
 * the shared window-system lifetime; each successful acquire needs one release
 * after its windows, input and presentation owners close.
 */
struct mh_window_system {
	const struct mh_window_ops	*ops;
	void				*context;
};

struct mh_window_ops {
	enum mh_error_code (*acquire)(void *context,
		const struct mh_window_system_options *options,
		struct mh_error		*error_out);
	void				(*release)(void *context);

	/*
	 * Failed open leaves output unchanged and cleans partial native state.
	 */
	enum mh_error_code (*open)(void *context, const char *title,
		unsigned int width, unsigned int height, unsigned int flags,
		void			**output, struct mh_error *error_out);
	void				(*destroy)(void *context, void *window);

	unsigned long	(*id)(void *context, const void *window);

	/* Optional operations report UNSUPPORTED when absent. */
	enum mh_error_code (*hide)(
		void	*context, void *window, struct mh_error *error_out);
	enum mh_error_code (*show)(
		void	*context, void *window, struct mh_error *error_out);
	enum mh_error_code (*raise)(
		void	*context, void *window, struct mh_error *error_out);
	enum mh_error_code (*title)(void *context, void *window,
		const char		*title, struct mh_error *error_out);

	enum mh_error_code (*modes)(void *context, unsigned int limit,
		mh_display_mode_visit visit, void *visitor,
		struct mh_error		*error_out);
	enum mh_error_code (*fullscreen)(void *context, void *window,
		int			enabled, struct mh_error *error_out);

	enum mh_error_code (*get_size)(void *context, void *window,
		unsigned int *width, unsigned int *height,
		struct mh_error		*error_out);
	enum mh_error_code (*set_size)(void *context, void *window,
		unsigned int width, unsigned int height,
		struct mh_error		*error_out);

	enum mh_error_code (*icon)(void *context, void *window,
		const struct mh_image	*image, struct mh_error *error_out);
};

/* Initialization performs no I/O; invalid tables leave system unchanged. */
enum mh_error_code	mh_window_system_init(struct mh_window_system *system,
	const struct mh_window_ops *ops, void *context,
	struct mh_error *error_out);

/*
 * Private backend bridges may retrieve the native token only with the creating
 * operation table. NULL/incompatible windows return NULL. No OS type escapes
 * the implementation. The token remains borrowed until window destruction.
 */
void	*mh_window_backend_context(const struct mh_window *window,
	const struct mh_window_ops *ops);

#endif
