#ifndef MH_ARGUMENTS_H
#define MH_ARGUMENTS_H

enum mh_argument_kind {
	MH_ARGUMENT_UNKNOWN,
	MH_ARGUMENT_FLAG,
	MH_ARGUMENT_VALUE
};

struct mh_argument_option {
	const char		*name;
	enum mh_argument_kind	kind;
};

typedef enum mh_argument_kind (*mh_argument_lookup)(const char *name);

enum mh_argument_result {
	MH_ARGUMENT_END,
	MH_ARGUMENT_OPTION,
	MH_ARGUMENT_POSITIONAL,
	MH_ARGUMENT_ERROR
};

/*
 * Borrows argv without changing it. No allocation, global state or output.
 * Options use --name or --name VALUE; lookup owns their names and arity.
 * A standalone -- ends option parsing; a lone - is a positional argument.
 * Values must be nonempty and cannot start with -- (use ./--file for a path).
 * Single-dash values, including negative numbers, reach caller validation.
 * Repeated options are returned in order; callers choose their policy.
 * On error, argument and message describe the failure; iteration stays failed.
 */
struct mh_arguments {
	int		argc;
	char		*const *argv;
	int		next, positional, failed;
	const char	*argument, *value, *message;
};

void			mh_arguments_init(struct mh_arguments *arguments,
	int argc, char *const argv[]);
enum mh_argument_result	mh_arguments_next(struct mh_arguments *arguments,
	mh_argument_lookup lookup);

/*
 * Tables end with {NULL, MH_ARGUMENT_UNKNOWN}. Exact names, no abbreviations.
 */
enum mh_argument_kind	mh_arguments_find(const char *name,
	const struct mh_argument_option *options);

/* Decimal digits only; leave output unchanged on invalid/overflowing input. */
int	mh_arguments_unsigned(const char *text, unsigned long limit,
	unsigned long *output);

#endif
