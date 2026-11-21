#include <stdio.h>
#include <stdlib.h>

#include "backends/storage/filesystem_windows_path.h"
#include "extract/hosts/application.h"

int
wmain(int argc, WCHAR *wide[])
{
	struct mh_error error;
	char **argv;
	int i, result;

	argv = calloc((size_t)argc + 1, sizeof(*argv));

	if (argv == NULL) {
		fputs("openmotorex: out of memory preparing arguments\n",
			stderr);
		return 1;
	}

	/* Arguments are paths (or --help); shared parsers use UTF-8 and '/'. */
	for (i = 0; i < argc; i++) {
		if (mh_windows_path_to_utf8(wide[i], &argv[i], &error) !=
			MH_ERROR_NONE)
			break;
	}

	if (i == argc)
		result = mh_extract_main(argc, argv, NULL, NULL);
	else {
		fprintf(stderr, "openmotorex: %s\n", error.message);
		result = 1;
	}

	while (i > 0)
		free(argv[--i]);

	free(argv);

	return result;
}
