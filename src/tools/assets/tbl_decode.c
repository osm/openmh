#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/tbl.h"
#include "engine/core/error.h"

int
main(int argc, char *argv[])
{
	struct mh_file file;
	unsigned char *text;
	size_t size;
	struct mh_error error;
	enum mh_error_code status;
	int failed;

	if (argc != 2 &&
		(argc != 4 || strcmp(argv[2], "--key") != 0 ||
			argv[3][0] == 0)) {
		fprintf(stderr,
			"Usage: mh-tbl-decode FILE [--key BASENAME] > decoded.txt\n");
		return 1;
	}

	status = mh_file_read(argv[1], &file, &error);

	if (status == MH_ERROR_NONE) {
		if (argc == 4)
			status = mh_tbl_decode_named(file.data, file.size,
				argv[3], &text, &size, &error);
		else
			status = mh_tbl_decode(
				file.data, file.size, &text, &size, &error);

		free(file.data);
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s: %s\n", argv[1], error.message);
		return 1;
	}

	failed = fwrite(text, 1, size, stdout) != size;

	if (fflush(stdout) != 0)
		failed = 1;

	free(text);

	return failed ? 1 : 0;
}
