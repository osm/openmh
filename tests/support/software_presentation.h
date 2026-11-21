#ifndef MH_TEST_SOFTWARE_PRESENTATION_H
#define MH_TEST_SOFTWARE_PRESENTATION_H

#include "backends/render/software_provider.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/window/backend.h"

struct mh_test_presentation {
	struct mh_window_system		windows;
	struct mh_canvas_provider	canvases;
	struct mh_software_output	output;
	struct mh_render_backend	backend;
	struct mh_window		*window;

	/* Borrowed pixels of the last opened/drawn canvas, or NULL. */
	unsigned char			*pixels;
	unsigned int width, height, references, opened, closed, frames,
		captures, results;
	unsigned long			windows_opened, windows_closed;
	int				fail_open, fail_draw, fail_present;
};

enum mh_error_code	mh_test_presentation_open(
	struct mh_test_presentation *test, struct mh_error *error_out);
void			mh_test_presentation_close(
	struct mh_test_presentation *test);

#endif
