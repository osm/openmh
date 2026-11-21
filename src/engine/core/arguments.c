#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/arguments.h"

void
mh_arguments_init(struct mh_arguments *arguments, int argc, char *const argv[])
{
	memset(arguments, 0, sizeof(*arguments));
	arguments->argc = argc;
	arguments->argv = argv;
	arguments->next = 1;
}

static enum mh_argument_result
fail(struct mh_arguments *arguments, const char *message)
{
	arguments->message = message;
	arguments->failed = 1;

	return MH_ARGUMENT_ERROR;
}

enum mh_argument_result
mh_arguments_next(struct mh_arguments *arguments, mh_argument_lookup lookup)
{
	enum mh_argument_kind kind;
	const char *text;

	if (arguments->failed)
		return MH_ARGUMENT_ERROR;

	arguments->argument = NULL;
	arguments->value = NULL;
	arguments->message = NULL;

	if (arguments->argc < 1 || arguments->argv == NULL || lookup == NULL)
		return fail(arguments, "invalid argument list");

	if (arguments->next == arguments->argc)
		return MH_ARGUMENT_END;

	text = arguments->argv[arguments->next++];

	if (text != NULL && !arguments->positional && strcmp(text, "--") == 0) {
		arguments->positional = 1;

		if (arguments->next == arguments->argc)
			return MH_ARGUMENT_END;

		text = arguments->argv[arguments->next++];
	}

	arguments->argument = text;

	if (text == NULL || *text == 0)
		return fail(arguments, "empty argument");

	if (arguments->positional || text[0] != '-' || text[1] == 0)
		return MH_ARGUMENT_POSITIONAL;

	kind = lookup(text);

	if (kind == MH_ARGUMENT_UNKNOWN)
		return fail(arguments, "unknown option");

	if (kind == MH_ARGUMENT_FLAG)
		return MH_ARGUMENT_OPTION;

	if (kind != MH_ARGUMENT_VALUE)
		return fail(arguments, "invalid option definition");

	if (arguments->next == arguments->argc)
		return fail(arguments, "option requires a value");

	text = arguments->argv[arguments->next];

	if (text == NULL || *text == 0 || strncmp(text, "--", 2) == 0)
		return fail(arguments, "option requires a nonempty value");

	arguments->value = text;
	arguments->next++;

	return MH_ARGUMENT_OPTION;
}

enum mh_argument_kind
mh_arguments_find(const char *name, const struct mh_argument_option *options)
{
	if (name == NULL || options == NULL)
		return MH_ARGUMENT_UNKNOWN;

	for (; options->name != NULL; options++) {
		if (strcmp(name, options->name) == 0)
			return options->kind;
	}

	return MH_ARGUMENT_UNKNOWN;
}

int
mh_arguments_unsigned(
	const char *text, unsigned long limit, unsigned long *output)
{
	char *end;
	unsigned long value;

	if (text == NULL || output == NULL || *text < '0' || *text > '9')
		return 0;

	errno = 0;
	value = strtoul(text, &end, 10);

	if (errno != 0 || *end != 0 || value > limit)
		return 0;

	*output = value;

	return 1;
}
