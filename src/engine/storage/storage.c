#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/storage/storage.h"

struct mh_storage {
	const struct mh_storage_ops	*ops;
	void				*context;
	enum mh_storage_status		status;
	struct mh_error			error;
	char				error_text[512];
};

static enum mh_storage_status
apply_result(struct mh_storage *storage, enum mh_storage_status status,
	struct mh_error error)
{
	if (error.code != MH_ERROR_NONE ||
		(status != MH_STORAGE_READY && status != MH_STORAGE_PENDING &&
			status != MH_STORAGE_FAILED))
		status = MH_STORAGE_FAILED;

	if (status == MH_STORAGE_FAILED) {
		if (error.code == MH_ERROR_NONE)
			mh_error_set(&error, MH_ERROR_FAILED,
				"storage operation failed");

		mh_error_copy(&storage->error, &error, storage->error_text,
			sizeof(storage->error_text));
	}

	storage->status = status;

	return status;
}

enum mh_error_code
mh_storage_create(const struct mh_storage_ops *ops, void *context,
	struct mh_storage **output, struct mh_error *error_out)
{
	struct mh_storage *storage;

	if (ops == NULL || ops->begin == NULL || ops->poll == NULL ||
		ops->cancel == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid storage provider");

	storage = (struct mh_storage *)calloc(1, sizeof(*storage));

	if (storage == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening storage");

	storage->ops = ops;
	storage->context = context;
	storage->status = MH_STORAGE_READY;
	*output = storage;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_storage_begin(struct mh_storage *storage,
	enum mh_storage_operation operation, struct mh_error *error_out)
{
	struct mh_error error;
	enum mh_storage_status status;

	if (storage == NULL ||
		(operation != MH_STORAGE_PREPARE &&
			operation != MH_STORAGE_COMMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid storage operation");

	if (storage->status == MH_STORAGE_PENDING)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"storage operation is already pending");

	storage->error = mh_success;
	storage->error_text[0] = 0;
	error = mh_success;
	status = storage->ops->begin(storage->context, operation, &error);
	apply_result(storage, status, error);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_storage_status
mh_storage_status(const struct mh_storage *storage)
{
	return storage == NULL ? MH_STORAGE_FAILED : storage->status;
}

enum mh_storage_status
mh_storage_step(struct mh_storage *storage)
{
	struct mh_error error;
	enum mh_storage_status status;

	if (storage == NULL)
		return MH_STORAGE_FAILED;

	if (storage->status != MH_STORAGE_PENDING)
		return storage->status;

	error = mh_success;
	status = storage->ops->poll(storage->context, &error);

	return apply_result(storage, status, error);
}

enum mh_error_code
mh_storage_error(const struct mh_storage *storage, struct mh_error *error_out)
{
	if (storage == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing storage");

	return mh_error_forward(error_out, &storage->error);
}

void
mh_storage_close(struct mh_storage *storage)
{
	if (storage == NULL)
		return;

	if (storage->status == MH_STORAGE_PENDING)
		storage->ops->cancel(storage->context);

	free(storage);
}
