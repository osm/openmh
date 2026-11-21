#ifndef MH_EMSCRIPTEN_RUNTIME_PROVIDER_H
#define MH_EMSCRIPTEN_RUNTIME_PROVIDER_H

/* One browser-thread loop owner. Starting returns to the embedding page. */
void	mh_browser_loop_start(void (*step)(void));
void	mh_browser_loop_cancel(void);

/* Monotonic milliseconds, independent of application suspension. */
double	mh_browser_loop_time(void);

#endif
