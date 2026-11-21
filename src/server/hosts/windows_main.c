#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "backends/text/windows.h"
#include "server/console/output.h"
#include "server/hosts/application.h"

int
wmain(int argc, WCHAR *wide[])
{
	char **argv, *at;
	int i, result;

	argv = calloc((size_t)argc + 1, sizeof(*argv));

	if (argv == NULL)
		return 1;

	for (i = 0; i < argc; i++) {
		if (mh_windows_text_to_utf8(wide[i], &argv[i], NULL) !=
			MH_ERROR_NONE)
			break;

		/*
		 * Keep the shared course-root parser's slash-separated path
		 * syntax.
		 */
		if (i > 0 &&
			(strcmp(argv[i - 1], "--config") == 0 ||
				strcmp(argv[i - 1], "--assets") == 0))
			for (at = argv[i]; *at != 0; at++)
				if (*at == '\\')
					*at = '/';
	}

	if (i == argc)
		result = mh_server_main(argc, argv);
	else {
		mh_server_console_puts(
			stderr, "Cannot prepare UTF-8 server arguments.");
		result = 1;
	}

	while (i > 0)
		free(argv[--i]);

	free(argv);

	return result;
}
