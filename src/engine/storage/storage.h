#ifndef MH_STORAGE_LIFECYCLE_H
#define MH_STORAGE_LIFECYCLE_H

#include "engine/core/error.h"
#include "interfaces/storage/storage.h"

struct mh_storage;
enum mh_error_code	mh_storage_create(const struct mh_storage_ops *ops,
	void *context, struct mh_storage **output, struct mh_error *error_out);

/*
 * Success means the request was accepted; status/step report its result.
 * Reject a second pending operation without changing the first.
 */
enum mh_error_code	mh_storage_begin(struct mh_storage *storage,
	enum mh_storage_operation operation, struct mh_error *error_out);
enum mh_storage_status	mh_storage_status(const struct mh_storage *storage);
enum mh_storage_status	mh_storage_step(struct mh_storage *storage);

/* Failure details remain owned until the next accepted begin or close. */
enum mh_error_code	mh_storage_error(const struct mh_storage *storage,
	struct mh_error *error_out);

/*
 * Close consumes the object and cancels pending work; it never starts a save.
 */
void	mh_storage_close(struct mh_storage *storage);

#endif
