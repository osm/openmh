#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "client/application/application.h"
#include "client/hosts/sdl_application.h"
#include "engine/core/error.h"
#include "engine/storage/storage.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "application storage line %d: %s\n",   \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct storage_fixture {
	const char			*settings;
	enum mh_storage_operation	operation;
	unsigned int			prepares, commits, polls, cancellations;
	int				ready, fail_prepare, fail_commit;
};

static int
file_exists(const char *path)
{
	FILE *file;

	file = fopen(path, "rb");

	if (file == NULL)
		return 0;

	fclose(file);

	return 1;
}

static enum mh_storage_status
begin_storage(void *context, enum mh_storage_operation operation,
	struct mh_error *error_out)
{
	struct storage_fixture *fixture;

	(void)error_out;
	fixture = (struct storage_fixture *)context;
	fixture->operation = operation;

	if (operation == MH_STORAGE_PREPARE)
		fixture->prepares++;
	else
		fixture->commits++;

	return MH_STORAGE_PENDING;
}

static enum mh_storage_status
poll_storage(void *context, struct mh_error *error_out)
{
	struct storage_fixture *fixture;

	fixture = (struct storage_fixture *)context;
	fixture->polls++;

	if (!fixture->ready)
		return MH_STORAGE_PENDING;

	if (fixture->operation == MH_STORAGE_PREPARE && fixture->fail_prepare) {
		mh_error_set(error_out, MH_ERROR_FAILED, "restore unavailable");
		return MH_STORAGE_FAILED;
	}

	if (fixture->operation == MH_STORAGE_COMMIT && fixture->fail_commit &&
		(fixture->fail_commit == 2 || file_exists(fixture->settings))) {
		mh_error_set(error_out, MH_ERROR_FAILED, "commit unavailable");
		return MH_STORAGE_FAILED;
	}

	return MH_STORAGE_READY;
}

static void
cancel_storage(void *context)
{
	struct storage_fixture *fixture;

	fixture = (struct storage_fixture *)context;
	fixture->cancellations++;
}

static const struct mh_storage_ops storage_ops = {
	begin_storage, poll_storage, cancel_storage};

int
main(int argc, char *argv[])
{
	struct mh_error diagnostic;

	struct storage_fixture fixture;
	struct mh_storage *storage;
	struct mh_application *application;
	struct mh_file saved, after;
	enum mh_app_status status;
	char path[1024];
	char *args[] = {"motorhead", "--debug-no-intro", "--debug-no-records",
		"--audio", "0", "--assets", NULL, "--debug-settings", NULL,
		"--debug-frames", "2"};

	unsigned int i, commits, polls;

	CHECK(argc == 3 &&
		strlen(argv[2]) + sizeof("/settings.cfg") <= sizeof(path));
	strcpy(path, argv[2]);
	strcat(path, "/settings.cfg");
	CHECK(!file_exists(path));
	args[6] = argv[1];
	args[8] = path;
	memset(&fixture, 0, sizeof(fixture));
	fixture.settings = path;
	CHECK(mh_storage_create(&storage_ops, &fixture, &storage, NULL) ==
		MH_ERROR_NONE);
	application = NULL;
	CHECK(mh_application_open_with_storage(
		0, NULL, storage, &application, NULL) != MH_ERROR_NONE);
	CHECK(application == NULL &&
		mh_storage_status(storage) == MH_STORAGE_READY);
	CHECK(mh_application_open_with_storage(
		(int)(sizeof(args) / sizeof(args[0])), args, storage,
		&application, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 5; i++) {
		CHECK(mh_application_step(application) == MH_APP_RUNNING);
		CHECK(mh_application_delay(application) == 1);
		CHECK(fixture.prepares == 1 && fixture.commits == 0 &&
			!file_exists(path));
	}

	CHECK(mh_application_close(application) != 0);
	CHECK(fixture.cancellations == 1 && !file_exists(path));

	fixture.ready = 1;
	CHECK(mh_storage_create(&storage_ops, &fixture, &storage, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_application_open_with_storage(
		(int)(sizeof(args) / sizeof(args[0])), args, storage,
		&application, NULL) == MH_ERROR_NONE);
	i = 0;

	while (fixture.commits == 0) {
		CHECK(mh_application_step(application) == MH_APP_RUNNING);
		CHECK(++i < 20);
	}

	CHECK(mh_storage_status(storage) == MH_STORAGE_PENDING &&
		!file_exists(path));
	CHECK(mh_application_close(application) != 0);
	CHECK(fixture.cancellations == 2 && !file_exists(path));

	fixture.ready = fixture.fail_prepare = 1;
	fixture.fail_commit = 2;
	CHECK(mh_storage_create(&storage_ops, &fixture, &storage, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_application_open_with_storage(
		(int)(sizeof(args) / sizeof(args[0])), args, storage,
		&application, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_step(application) == MH_APP_RUNNING);
	CHECK(mh_application_step(application) == MH_APP_FAILED);
	mh_application_storage_error(application, &diagnostic);
	CHECK(strcmp(diagnostic.message, "restore unavailable") == 0);
	polls = fixture.polls;
	CHECK(mh_application_step(application) == MH_APP_FAILED);
	CHECK(fixture.polls == polls && !file_exists(path));
	fixture.fail_prepare = 0;
	CHECK(mh_application_retry_storage(application, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_storage_error(application, NULL) == MH_ERROR_NONE);
	i = 0;

	do {
		status = mh_application_step(application);
		CHECK(++i < 20);
	} while (status == MH_APP_RUNNING);

	CHECK(status == MH_APP_FAILED && !file_exists(path));
	mh_application_storage_error(application, &diagnostic);
	CHECK(strcmp(diagnostic.message, "commit unavailable") == 0);
	commits = fixture.commits;
	fixture.fail_commit = 1;
	CHECK(mh_application_retry_storage(application, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_step(application) == MH_APP_RUNNING);
	CHECK(mh_application_step(application) == MH_APP_RUNNING);
	CHECK(fixture.commits == commits + 1 && !file_exists(path));
	i = 0;

	do {
		status = mh_application_step(application);
		CHECK(++i < 40);
	} while (status == MH_APP_RUNNING && !file_exists(path));

	/*
	 * Final local saves have completed, but durability is still
	 * outstanding.
	 */
	CHECK(status == MH_APP_RUNNING && file_exists(path));
	CHECK(mh_application_delay(application) == 1);
	CHECK(mh_file_read(path, &saved, NULL) == MH_ERROR_NONE &&
		saved.size != 0);
	fixture.ready = 0;
	commits = fixture.commits;

	for (i = 0; i < 5; i++) {
		CHECK(mh_application_step(application) == MH_APP_RUNNING);
		CHECK(fixture.commits == commits);
	}

	fixture.ready = 1;
	CHECK(mh_application_step(application) == MH_APP_FAILED);
	mh_application_storage_error(application, &diagnostic);
	CHECK(strcmp(diagnostic.message, "commit unavailable") == 0);
	CHECK(mh_application_step(application) == MH_APP_FAILED);
	fixture.fail_commit = 0;
	CHECK(mh_application_retry_storage(application, NULL) == MH_ERROR_NONE);
	CHECK(mh_application_step(application) == MH_APP_RUNNING);
	CHECK(fixture.commits == commits + 1);
	CHECK(mh_application_step(application) == MH_APP_DONE);
	CHECK(mh_application_step(application) == MH_APP_DONE);
	CHECK(fixture.commits == commits + 1);
	CHECK(mh_application_retry_storage(application, NULL) != MH_ERROR_NONE);
	CHECK(mh_file_read(path, &after, NULL) == MH_ERROR_NONE);
	CHECK(saved.size == after.size &&
		memcmp(saved.data, after.data, saved.size) == 0);
	free(saved.data);
	free(after.data);
	CHECK(mh_application_close(application) == 0);
	CHECK(fixture.cancellations == 2);
	CHECK(remove(path) == 0);
	puts("Application waits for storage readiness and final durability; failed saves retry without another frame");

	return 0;
}
