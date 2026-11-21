#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/horn.h"
#include "engine/audio/horn.h"
#include "engine/core/error.h"

static int
inspect_parameters(int percentages, int preview)
{
	struct mh_horn horn;
	struct mh_error error;
	enum mh_error_code status;
	double gain, pitch;
	char text[128];
	int scanned;

	memset(&horn, 0, sizeof(horn));

	while ((scanned = scanf("%lf %lf", &horn.volume, &horn.pitch)) == 2) {
		status = MH_ERROR_NONE;

		if (percentages) {
			sprintf(text, "VolumeChange %.17g\nPitchChange %.17g",
				horn.volume, horn.pitch);
			status = mh_horn_chf_decode((const unsigned char *)text,
				strlen(text), 0, &horn, &error);
		}

		if (status == MH_ERROR_NONE) {
			if (preview)
				status = mh_horn_preview_parameters(
					&horn, &gain, &pitch, &error);
			else
				status = mh_horn_parameters(
					&horn, &gain, &pitch, &error);
		}

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "%s\n", error.message);
			return 1;
		}

		if (percentages)
			printf("%.17g %.17g ", horn.volume, horn.pitch);

		printf("%.17g %.17g\n", gain, pitch);
	}

	return scanned == EOF && !ferror(stdin) && !ferror(stdout) ? 0 : 1;
}

static int
inspect_horn(const char *path, unsigned int index)
{
	struct mh_horn horn;
	struct mh_file file;
	struct mh_error error;
	enum mh_error_code status;
	double gain, pitch;

	memset(&file, 0, sizeof(file));
	status = mh_file_read(path, &file, &error);

	if (status == MH_ERROR_NONE)
		status = mh_horn_chf_decode(
			file.data, file.size, index, &horn, &error);

	free(file.data);

	if (status == MH_ERROR_NONE)
		status = mh_horn_parameters(&horn, &gain, &pitch, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s: %s\n", path, error.message);
		return 1;
	}

	printf("%s\t%s\t%.17g\t%.17g\t%.17g\t%.17g\n", horn.name, horn.sample,
		horn.volume, horn.pitch, gain, pitch);

	return 0;
}

int
main(int argc, char *argv[])
{
	int i, percentages, preview;

	preview = argc == 2 && strcmp(argv[1], "--preview-percentages") == 0;
	percentages =
		preview || (argc == 2 && strcmp(argv[1], "--percentages") == 0);

	if (argc == 2 &&
		(percentages || strcmp(argv[1], "--parameters") == 0)) {
		return inspect_parameters(percentages, preview);
	}

	if (argc < 2) {
		fprintf(stderr,
			"Usage: mh-horn-info FILE.chf [...] | --parameters | --percentages | --preview-percentages\n");
		return 1;
	}

	for (i = 1; i < argc; i++) {
		if (inspect_horn(argv[i], (unsigned int)(i - 1) % MH_HORNS))
			return 1;
	}

	return ferror(stdout) ? 1 : 0;
}
