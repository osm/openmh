#include <stdio.h>

#include "../support/tron_render.h"
#include "backends/render/webgl_provider.h"
#include "backends/window/sdl_provider.h"

int	mh_webgl_tron_check(void);

int
mh_webgl_tron_check(void)
{
	const struct mh_window_system *system;
	struct mh_window *window;
	struct mh_error error;
	int failed;

	system = mh_sdl_window_system();
	window = NULL;

	if (mh_window_system_acquire(system, NULL, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	failed = 1;

	if (mh_window_open(system, "WebGL TRON check", 960, 640, 0, &window,
		&error) == MH_ERROR_NONE) {
		failed = mh_tron_render_check(
			mh_webgl_glide_backend(), window, NULL);
		failed |= mh_tron_render_check(
			mh_webgl_direct3d_backend(), window, NULL);
		failed |= mh_tron_render_check(
			mh_webgl_powervr_backend(), window, NULL);
	} else
		fprintf(stderr, "%s\n", error.message);

	mh_window_destroy(window);
	mh_window_system_release(system);

	return failed;
}

int
main(void)
{
	return 0;
}
