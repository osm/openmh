#ifndef MH_SETTINGS_PARSE_H
#define MH_SETTINGS_PARSE_H

/*
 * Shared legacy settings syntax. Callers own staged state and error rollback.
 */
#include <stddef.h>

#include "engine/core/error.h"

int	mh_cfg_boolean(const char *value);
int	mh_cfg_number(const char *text, double *output);
int	mh_cfg_integer(long double value, unsigned long *output);
void	mh_cfg_choice(const char *value, const char *const *names,
	unsigned int count, unsigned int *target);

/*
 * Visit every physical line, including empty/comment-only lines. Tokens borrow
 * a mutable scratch line and expire when the callback returns. Stops at its
 * first error; completed callbacks are not rolled back here.
 */
enum mh_error_code	mh_cfg_parse(const unsigned char *text, size_t size,
	enum mh_error_code (*visit)(char **tokens, unsigned int count, void *context, struct mh_error *error_out),
	void *context, struct mh_error *error_out);

#endif
