#include <stdlib.h>

#include "interfaces/archive/archive_backend.h"

struct mh_archive {
	const struct mh_archive_ops	*ops;
	void				*context;
};

enum mh_error_code
mh_archive_create(const struct mh_archive_ops *ops, void *context,
	struct mh_archive **output, struct mh_error *error_out)
{
	struct mh_archive *archive;

	if (ops == NULL || ops->count == NULL || ops->entry == NULL ||
		ops->read == NULL || ops->close == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid archive provider");

	archive = malloc(sizeof(*archive));

	if (archive == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening archive");

	archive->ops = ops;
	archive->context = context;
	*output = archive;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

size_t
mh_archive_count(const struct mh_archive *archive)
{
	return archive == NULL ? 0 : archive->ops->count(archive->context);
}

const struct mh_archive_entry *
mh_archive_entry(const struct mh_archive *archive, size_t index)
{
	if (index >= mh_archive_count(archive))
		return NULL;

	return archive->ops->entry(archive->context, index);
}

enum mh_error_code
mh_archive_read(struct mh_archive *archive, size_t index, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	const struct mh_archive_entry *entry;

	entry = mh_archive_entry(archive, index);

	if (entry == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid archive entry request");

	if (entry->size > limit)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"archive member exceeds allocation limit");

	return archive->ops->read(archive->context, index, output, error_out);
}

void
mh_archive_close(struct mh_archive *archive)
{
	if (archive == NULL)
		return;

	archive->ops->close(archive->context);
	free(archive);
}
