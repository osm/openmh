#include <stddef.h>

#include "backends/storage/indexeddb_provider.h"
#include "engine/storage/storage.h"

/* The browser owns the cache and request lifetime; no C pointers cross over. */
int	mh_indexeddb_begin(int prepare);
int	mh_indexeddb_poll(void);
void	mh_indexeddb_cancel(void);

static enum mh_storage_status
result(int status, struct mh_error *error_out)
{
	if (status < 0) {
		mh_error_set(error_out, MH_ERROR_IO,
			"browser save storage failed; local changes are retained for retry");
		return MH_STORAGE_FAILED;
	}

	return status == 0 ? MH_STORAGE_READY : MH_STORAGE_PENDING;
}

static enum mh_storage_status
begin(void *context, enum mh_storage_operation operation,
	struct mh_error *error_out)
{
	(void)context;

	return result(
		mh_indexeddb_begin(operation == MH_STORAGE_PREPARE), error_out);
}

static enum mh_storage_status
poll(void *context, struct mh_error *error_out)
{
	(void)context;

	return result(mh_indexeddb_poll(), error_out);
}

static void
cancel(void *context)
{
	(void)context;
	mh_indexeddb_cancel();
}

enum mh_error_code
mh_indexeddb_storage_open(
	struct mh_storage **output, struct mh_error *error_out)
{
	static const struct mh_storage_ops operations = {begin, poll, cancel};

	return mh_storage_create(&operations, NULL, output, error_out);
}
