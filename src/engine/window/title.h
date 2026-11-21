#ifndef MH_ENGINE_WINDOW_TITLE_H
#define MH_ENGINE_WINDOW_TITLE_H

struct mh_window;

/* Select captions using the owning window's explicit diagnostic option. */
const char	*mh_window_title(const struct mh_window *window,
	const char *title, const char *diagnostic);

#endif
