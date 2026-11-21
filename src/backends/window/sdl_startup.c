#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include "backends/window/sdl_provider.h"

int
mh_sdl_run_application(
	int argc, char *argv[], int (*run)(int argc, char *argv[]))
{
#ifdef _WIN32
	/* The CRT's narrow argv uses the system code page, not UTF-8. */
	(void)argc;

	(void)argv;
	return SDL_RunApp(0, NULL, run, NULL);
#else

	return SDL_RunApp(argc, argv, run, NULL);
#endif
}
