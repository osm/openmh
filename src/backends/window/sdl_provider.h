#ifndef MH_BACKENDS_WINDOW_SDL_PROVIDER_H
#define MH_BACKENDS_WINDOW_SDL_PROVIDER_H

#include "interfaces/window/backend.h"
#include "interfaces/window/window.h"

/*
 * Run once on the process's initial thread. SDL prepares platform startup and
 * supplies UTF-8 arguments from the OS command line on Windows. Other native
 * hosts retain their supplied arguments. Returns the application's exit code.
 * This does not acquire a window or initialize audio/video subsystems.
 */
int	mh_sdl_run_application(int argc, char *argv[],
	int (*run)(int argc, char *argv[]));

/*
 * Process-lifetime SDL descriptor; acquisition is explicit and reference
 * counted.
 */
const struct mh_window_system	*mh_sdl_window_system(void);

/* Compatibility helpers select the SDL provider for native tools. */
/*
 * Select offscreen windows before initialization, for deterministic captures.
 */
void	mh_sdl_window_offscreen(int preserve_environment);

/* Each successful acquire must be paired with release. */
int	mh_sdl_window_acquire(void);

void	mh_platform_shutdown(void);

/*
 * Query after a failed backend operation. Reports MH_ERROR_BACKEND; the message
 * is borrowed until the next backend operation on this thread.
 */
enum mh_error_code	mh_platform_error(struct mh_error *error_out);

struct mh_window	*mh_window_create(const char *title, unsigned int width,
	unsigned int height, unsigned int flags);

#endif
