#include <stddef.h>

#ifdef MH_DIRECTORY_SYNC
#include <errno.h>

#include "backends/storage/filesystem_posix_sync.h"
#endif

#include "backends/storage/filesystem_provider.h"
#include "engine/core/error.h"
#include "engine/storage/storage.h"
#include "interfaces/storage/storage.h"

static enum mh_storage_status
begin(void *context, enum mh_storage_operation operation,
	struct mh_error *error)
{
	(void)context;

#ifdef MH_DIRECTORY_SYNC
	if (operation == MH_STORAGE_COMMIT && mh_directory_sync_retry() != 0) {
		mh_error_system(
			error, errno, "cannot sync saved directory changes");
		return MH_STORAGE_FAILED;
	}

#else
	(void)operation;
	(void)error;
#endif

	return MH_STORAGE_READY;
}

static enum mh_storage_status
poll(void *context, struct mh_error *error)
{
	(void)context;
	(void)error;

	return MH_STORAGE_READY;
}

static void
cancel(void *context)
{
	(void)context;
}

static const struct mh_storage_ops native_ops = {begin, poll, cancel};

enum mh_error_code
mh_storage_open(struct mh_storage **output, struct mh_error *error_out)
{
	return mh_storage_create(&native_ops, NULL, output, error_out);
}
