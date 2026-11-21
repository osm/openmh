#ifndef MH_ASSETS_DEMO_DEFINITION_H
#define MH_ASSETS_DEMO_DEFINITION_H

#include <stddef.h>

#include "engine/core/error.h"

#define MH_DEFINITION_BYTES 16384

struct mh_definition_text {
	unsigned char	data[MH_DEFINITION_BYTES];
	size_t		size;
};

enum mh_definition_kind {
	MH_DEFINITION_STRING,
	MH_DEFINITION_DIRECTORY,
	MH_DEFINITION_PAIR,
	MH_DEFINITION_INTEGER,
	MH_DEFINITION_FLOAT,
	MH_DEFINITION_COLOR,
	MH_DEFINITION_BYTES_RGB
};

struct mh_definition_field {
	const char		*name;
	size_t			offset;
	enum mh_definition_kind	kind;
	unsigned int		count;
};

/*
 * Private demo-record normalization helpers. Callers validate record and field
 * spans first.
 */
enum mh_error_code	mh_definition_write(struct mh_definition_text *text,
	const unsigned char *record, const struct mh_definition_field *field,
	struct mh_error *error_out);
enum mh_error_code	mh_definition_paths(struct mh_definition_text *text,
	const unsigned char *names, size_t size, const char *key,
	struct mh_error *error_out);

#endif
