#ifndef MH_TRON_RENDER_CHECK_H
#define MH_TRON_RENDER_CHECK_H

struct mh_render_backend;
struct mh_window;

/* Sequential fixture; NULL prefix checks pixels without saving captures. */
int	mh_tron_render_check(const struct mh_render_backend *backend,
	struct mh_window *window, const char *prefix);

#endif
