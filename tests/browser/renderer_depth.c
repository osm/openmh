#include <emscripten.h>
#include <string.h>

#include "interfaces/render/renderer.h"
#include "renderer_depth.h"

int
mh_webgl_test_read_depth(
	struct mh_renderer *renderer, unsigned char *visible, size_t count)
{
	unsigned int attempt;
	size_t i;

	/*
	 * These fixtures queue active probes; untouched bytes mean not ready.
	 */
	memset(visible, 255, count);

	for (attempt = 0; attempt < 120; attempt++) {
		if (!mh_renderer_depth_read(renderer, visible, count))
			return 0;

		for (i = 0; i < count && visible[i] != 255; i++)
			;

		if (i == count)
			return 1;

		/*
		 * WebGL fences can signal only after yielding to the browser.
		 */
		emscripten_sleep(16);
	}

	return 0;
}
