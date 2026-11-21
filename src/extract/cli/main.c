#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/arguments.h"
#include "extract/cli/extract.h"
#include "extract/hosts/application.h"

static void
usage(void)
{
	static const char *const lines[] = {
		("OpenMH asset extractor " MH_BUILD_VERSION), "",
		"Usage: openmotorex [options] SOURCE [PATCH ...] DESTINATION",
		"", "Options",
		"  --help                           Show this help and exit.",
		"", "Arguments",
		"  SOURCE                           Demo installer, retail CUE/TOC/ISO, CD drive",
		"                                   (Linux/Windows), copied disc or prepared game",
		"                                   directory.",
		"  PATCH                            Supported patch installer(s), applied in",
		"                                   order after the base source.",
		"  DESTINATION                      New game directory or .ass package.",
		"", "Directory options",
		"  --keep-original                  Keep installer and unpacked source files",
		"                                   under Original/ (default: discarded).",
		"", "Package options (.ass output only; may be combined)",
		"  --compact                        Convert music to Ogg Opus and movies to",
		"                                   VP9/Opus WebM.",
		"  --exclude-music                  Omit CD music.",
		"  --exclude-movies                 Omit opening and league movies.",
		"", "Separate patch package (recommended)",
		"  openmotorex motorp30.exe patch30.ass",
		"  Use a new output filename; place it beside the base game package.",
		"  Remove the patch package to return to the base game.", "",
		"Patching an existing installation",
		"  openmotorex motorp30.exe Motorhead",
		"  openmotorex motorp30.exe openmotor.ass", "", "Notes",
		"  Options must precede source paths. Use -- before paths beginning with -.",
		"  motorp30.exe is cumulative; earlier patches and 3.1 are not needed.",
		"  Close the game before patching. Saves are preserved; the destination",
		"  is replaced only after the complete patched output is ready.",
		"", "Examples", "  openmotorex Motorhead.cue Motorhead",
		"  openmotorex --compact /dev/sr0 openmotor.ass",
		"  openmotorex --compact D: openmotor.ass",
		"  openmotorex --compact D: motorp30.exe openmotor.ass",
		"  openmotorex --compact motorhead.toc openmotor.ass",
		"  openmotorex --compact Motorhead openmotor.ass",
		"  openmotorex --exclude-music --exclude-movies Motorhead small.ass",
		""};

	size_t i;

	for (i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
		puts(lines[i]);
}

static enum mh_argument_kind
argument_kind(const char *name)
{
	static const struct mh_argument_option options[] = {
		{"--compact", MH_ARGUMENT_FLAG},
		{"--keep-original", MH_ARGUMENT_FLAG},
		{"--exclude-music", MH_ARGUMENT_FLAG},
		{"--exclude-movies", MH_ARGUMENT_FLAG},
		{"--help", MH_ARGUMENT_FLAG}, {NULL, MH_ARGUMENT_UNKNOWN}};

	return mh_arguments_find(name, options);
}

static int
read_options(int argc, char *argv[], struct mh_extract_options *options,
	const char **paths, size_t *count)
{
	struct mh_arguments arguments;
	enum mh_argument_result result;

	memset(options, 0, sizeof(*options));
	*count = 0;
	mh_arguments_init(&arguments, argc, argv);

	while ((result = mh_arguments_next(&arguments, argument_kind)) !=
		MH_ARGUMENT_END) {
		if (result == MH_ARGUMENT_ERROR) {
			fprintf(stderr, "openmotorex: %s: %s\n",
				arguments.message,
				arguments.argument == NULL
					? ""
					: arguments.argument);
			return -1;
		}

		if (result == MH_ARGUMENT_POSITIONAL) {
			paths[(*count)++] = arguments.argument;
			continue;
		}

		if (strcmp(arguments.argument, "--help") == 0) {
			usage();
			return 0;
		}

		if (*count != 0) {
			fputs("openmotorex: options must precede source paths; use -- for paths beginning with -\n",
				stderr);
			return -1;
		}

		if (strcmp(arguments.argument, "--compact") == 0)
			options->compact = 1;
		else if (strcmp(arguments.argument, "--keep-original") == 0)
			options->keep_original = 1;
		else if (strcmp(arguments.argument, "--exclude-music") == 0)
			options->exclude_music = 1;
		else if (strcmp(arguments.argument, "--exclude-movies") == 0)
			options->exclude_movies = 1;
	}

	if (*count < 2) {
		fputs("openmotorex: expected a source and destination\n",
			stderr);
		return -1;
	}

	return 1;
}

int
mh_extract_main(
	int argc, char *argv[], mh_extract_progress progress, void *context)
{
	struct mh_error error;
	struct mh_extract_options options;
	const char **paths;
	size_t count, i;
	int patched, parsed, result;

	paths = calloc(argc > 0 ? (size_t)argc : 1, sizeof(*paths));

	if (paths == NULL) {
		fputs("openmotorex: out of memory reading arguments\n", stderr);
		return 1;
	}

	parsed = read_options(argc, argv, &options, paths, &count);

	if (parsed <= 0) {
		free(paths);

		if (parsed < 0)
			fputs("Try 'openmotorex --help' for usage.\n", stderr);

		return parsed < 0 ? 1 : 0;
	}

	printf("OpenMH asset extractor %s\n\n", MH_BUILD_VERSION);
	printf("  Source: %s\n", paths[0]);

	for (i = 1; i < count - 1; i++)
		printf("  Patch:  %s\n", paths[i]);

	printf("  Output: %s\n", paths[count - 1]);

	if (options.compact)
		puts("  Media:  compact encoding");

	if (options.exclude_music)
		puts("  Omit:   CD music");

	if (options.exclude_movies)
		puts("  Omit:   opening and league movies");

	putchar('\n');
	fflush(stdout);

	result = mh_extract_run(paths, count - 1, paths[count - 1], &options,
			&patched, progress, context, &error) != MH_ERROR_NONE;
	free(paths);

	if (result)
		fprintf(stderr, "\nopenmotorex: %s\n", error.message);

	return result;
}
