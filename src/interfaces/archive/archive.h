#ifndef MH_INTERFACES_ARCHIVE_H
#define MH_INTERFACES_ARCHIVE_H

#include "engine/core/error.h"
#include "engine/core/file.h"

enum {
	MH_ARCHIVE_PATH_SIZE = 1024,
	MH_ARCHIVE_MEMBER_LIMIT = 10000
};

struct mh_archive;

struct mh_archive_entry {
	char	path[MH_ARCHIVE_PATH_SIZE];
	size_t	size;
};

/*
 * Read-only archive. Entry paths are slash-separated relative file paths;
 * no empty/dot/parent components, absolute names or duplicate folded names.
 * Provider-owned entries remain valid until close. Reads return malloc-owned
 * bytes, preserve output on failure and enforce the caller's allocation limit.
 * Calls are serialized. A provider cannot publish files or execute entries.
 */
size_t				 mh_archive_count(
	const struct mh_archive *archive);
const struct mh_archive_entry	*mh_archive_entry(
	const struct mh_archive *archive, size_t index);
enum mh_error_code		 mh_archive_read(struct mh_archive *archive,
	size_t index, size_t limit, struct mh_file *output,
	struct mh_error *error_out);
void				 mh_archive_close(struct mh_archive *archive);

#endif
