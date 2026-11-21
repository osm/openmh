#ifndef MH_INTERFACES_ARCHIVE_BACKEND_H
#define MH_INTERFACES_ARCHIVE_BACKEND_H

#include "interfaces/archive/archive.h"

struct mh_archive_ops {
	size_t			(*count)(const void *context);
	const struct mh_archive_entry *(*entry)(
		const void	*context, size_t index);
	enum mh_error_code (*read)(void *context, size_t index,
		struct mh_file	*output, struct mh_error *error_out);
	void			(*close)(void *context);
};

/* Takes ownership of context only on success; ops must outlive the archive. */
enum mh_error_code	mh_archive_create(const struct mh_archive_ops *ops,
	void *context, struct mh_archive **output, struct mh_error *error_out);

#endif
