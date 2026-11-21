#include "engine/window/title.h"
#include "interfaces/window/window.h"

const char *
mh_window_title(const struct mh_window *window, const char *title,
	const char *diagnostic)
{
	return mh_window_diagnostic_titles(window) ? diagnostic : title;
}
