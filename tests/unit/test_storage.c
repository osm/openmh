#include <stdio.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "engine/core/error.h"
#include "engine/storage/storage.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "storage line %d: %s\n", __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct provider {
	enum mh_storage_operation	operation;
	unsigned int			begins, polls, cancellations;
	int				complete, fail;
	char				error[32];
};

static enum mh_storage_status
begin(void *context, enum mh_storage_operation operation,
	struct mh_error *error)
{
	struct provider *provider;

	(void)error;
	provider = (struct provider *)context;
	provider->operation = operation;
	provider->begins++;

	return MH_STORAGE_PENDING;
}

static enum mh_storage_status
poll(void *context, struct mh_error *error)
{
	struct provider *provider;

	provider = (struct provider *)context;
	provider->polls++;

	if (!provider->complete)
		return MH_STORAGE_PENDING;

	if (provider->fail) {
		strcpy(provider->error, "durability failure");
		mh_error_set(error, MH_ERROR_PERMISSION, provider->error);
		return MH_STORAGE_FAILED;
	}

	return MH_STORAGE_READY;
}

static void
cancel(void *context)
{
	struct provider *provider;

	provider = (struct provider *)context;
	provider->cancellations++;
}

static const struct mh_storage_ops ops = {begin, poll, cancel};

int
main(void)
{
	struct mh_error diagnostic;
	struct provider provider;
	struct mh_storage *storage;
	unsigned int polls;

	memset(&provider, 0, sizeof(provider));
	storage = NULL;
	CHECK(mh_storage_create(NULL, &provider, &storage, NULL) !=
			MH_ERROR_NONE &&
		storage == NULL);
	CHECK(mh_storage_create(&ops, &provider, &storage, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_storage_status(storage) == MH_STORAGE_READY);
	CHECK(mh_storage_begin(storage, MH_STORAGE_PREPARE, NULL) ==
		MH_ERROR_NONE);
	CHECK(provider.begins == 1 && provider.operation == MH_STORAGE_PREPARE);
	CHECK(mh_storage_step(storage) == MH_STORAGE_PENDING);
	CHECK(mh_storage_begin(storage, MH_STORAGE_COMMIT, NULL) !=
		MH_ERROR_NONE);
	CHECK(provider.begins == 1 &&
		mh_storage_status(storage) == MH_STORAGE_PENDING);
	provider.complete = 1;
	CHECK(mh_storage_step(storage) == MH_STORAGE_READY);
	polls = provider.polls;
	CHECK(mh_storage_step(storage) == MH_STORAGE_READY &&
		provider.polls == polls);
	CHECK(mh_storage_begin(storage, MH_STORAGE_COMMIT, NULL) ==
		MH_ERROR_NONE);
	provider.fail = 1;
	CHECK(mh_storage_step(storage) == MH_STORAGE_FAILED);
	memset(provider.error, 'x', sizeof(provider.error));
	CHECK(mh_storage_error(storage, NULL) == MH_ERROR_PERMISSION);
	mh_storage_error(storage, &diagnostic);
	CHECK(strcmp(diagnostic.message, "durability failure") == 0);
	polls = provider.polls;
	CHECK(mh_storage_step(storage) == MH_STORAGE_FAILED &&
		provider.polls == polls);
	provider.fail = 0;
	CHECK(mh_storage_begin(storage, MH_STORAGE_COMMIT, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_storage_error(storage, NULL) == MH_ERROR_NONE);
	CHECK(mh_storage_step(storage) == MH_STORAGE_READY);
	CHECK(provider.begins == 3 && provider.cancellations == 0);
	mh_storage_close(storage);
	CHECK(provider.cancellations == 0);
	CHECK(mh_storage_create(&ops, &provider, &storage, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_storage_begin(storage, MH_STORAGE_PREPARE, NULL) ==
		MH_ERROR_NONE);
	mh_storage_close(storage);
	CHECK(provider.cancellations == 1);
	CHECK(mh_storage_open(&storage, NULL) == MH_ERROR_NONE);
	CHECK(mh_storage_begin(storage, MH_STORAGE_PREPARE, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_storage_status(storage) == MH_STORAGE_READY);
	CHECK(mh_storage_begin(storage, MH_STORAGE_COMMIT, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_storage_status(storage) == MH_STORAGE_READY);
	mh_storage_close(storage);
	puts("Storage pending completion, failure retention, retry and cancellation passed");

	return 0;
}
