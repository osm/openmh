#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/arguments.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "arguments:%d: %s\n", __LINE__, #x);   \
			return 1;                                              \
		}                                                              \
	} while (0)

static enum mh_argument_kind
lookup(const char *name)
{
	static const struct mh_argument_option options[] = {
		{"--flag", MH_ARGUMENT_FLAG}, {"--value", MH_ARGUMENT_VALUE},
		{NULL, MH_ARGUMENT_UNKNOWN}};

	return mh_arguments_find(name, options);
}

static int
check_iteration(void)
{
	char *argv[] = {"program", "--flag", "--value", "hello world", "file",
		"--value", "-42", "--", "--flag", "--", "-"};

	char *before[sizeof(argv) / sizeof(argv[0])];
	struct mh_arguments arguments, other;
	char *empty[] = {"program", "--"};

	memcpy(before, argv, sizeof(argv));
	mh_arguments_init(
		&arguments, (int)(sizeof(argv) / sizeof(argv[0])), argv);
	CHECK(mh_arguments_next(&arguments, lookup) == MH_ARGUMENT_OPTION);
	CHECK(strcmp(arguments.argument, "--flag") == 0 &&
		arguments.value == NULL);
	CHECK(mh_arguments_next(&arguments, lookup) == MH_ARGUMENT_OPTION);
	CHECK(arguments.value == argv[3]);
	mh_arguments_init(&other, 2, empty);
	CHECK(mh_arguments_next(&other, lookup) == MH_ARGUMENT_END);
	CHECK(mh_arguments_next(&arguments, lookup) == MH_ARGUMENT_POSITIONAL);
	CHECK(arguments.argument == argv[4] && arguments.value == NULL);
	CHECK(mh_arguments_next(&arguments, lookup) == MH_ARGUMENT_OPTION);
	CHECK(strcmp(arguments.value, "-42") == 0);
	CHECK(mh_arguments_next(&arguments, lookup) == MH_ARGUMENT_POSITIONAL);
	CHECK(strcmp(arguments.argument, "--flag") == 0);
	CHECK(mh_arguments_next(&arguments, lookup) == MH_ARGUMENT_POSITIONAL);
	CHECK(strcmp(arguments.argument, "--") == 0);
	CHECK(mh_arguments_next(&arguments, lookup) == MH_ARGUMENT_POSITIONAL);
	CHECK(strcmp(arguments.argument, "-") == 0);
	CHECK(mh_arguments_next(&arguments, lookup) == MH_ARGUMENT_END);
	CHECK(mh_arguments_next(&arguments, lookup) == MH_ARGUMENT_END);
	CHECK(memcmp(before, argv, sizeof(argv)) == 0);

	return 0;
}

static int
check_errors(void)
{
	char *invalid[][3] = {{"program", "--unknown", NULL},
		{"program", "--f", NULL}, {"program", "-f", NULL},
		{"program", "--flag=1", NULL}, {"program", "--value", NULL},
		{"program", "--value", ""}, {"program", "--value", "--flag"},
		{"program", "--value", "--"}, {"program", "", NULL},
		{"program", NULL, NULL}};

	struct mh_arguments arguments;
	const char *message;
	size_t i;

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		mh_arguments_init(
			&arguments, invalid[i][2] == NULL ? 2 : 3, invalid[i]);
		CHECK(mh_arguments_next(&arguments, lookup) ==
			MH_ARGUMENT_ERROR);
		CHECK(arguments.message != NULL && arguments.value == NULL);
		message = arguments.message;
		CHECK(mh_arguments_next(&arguments, lookup) ==
			MH_ARGUMENT_ERROR);
		CHECK(arguments.message == message &&
			arguments.argument == invalid[i][1]);
	}

	mh_arguments_init(&arguments, 0, NULL);
	CHECK(mh_arguments_next(&arguments, lookup) == MH_ARGUMENT_ERROR);

	return 0;
}

static int
check_numbers(void)
{
	static const char *const invalid[] = {"", "-1", "+1", " 1", "1 ",
		"0x10", "1.5", "1x",
		"999999999999999999999999999999999999999999999999999999999999999999"};

	unsigned long value;
	char maximum[3 * sizeof(unsigned long) + 1];
	size_t i;

	CHECK(mh_arguments_unsigned("0", 0, &value) && value == 0);
	CHECK(mh_arguments_unsigned("0012", 12, &value) && value == 12);
	CHECK(!mh_arguments_unsigned("13", 12, &value) && value == 12);
	sprintf(maximum, "%lu", ULONG_MAX);
	CHECK(mh_arguments_unsigned(maximum, ULONG_MAX, &value) &&
		value == ULONG_MAX);

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		value = 71;
		CHECK(!mh_arguments_unsigned(invalid[i], ULONG_MAX, &value) &&
			value == 71);
	}

	return 0;
}

int
main(void)
{
	CHECK(check_iteration() == 0);
	CHECK(check_errors() == 0);
	CHECK(check_numbers() == 0);

	return 0;
}
