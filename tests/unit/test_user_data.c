#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/settings_catalog.h"
#include "backends/storage/memory_user_data.h"
#include "engine/storage/settings_file.h"
#include "engine/storage/storage.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "user data:%d: %s\n", __LINE__, #x);   \
			return 0;                                              \
		}                                                              \
	} while (0)

#define SETTINGS_KEY "preferences/MotorHead.CFG"

/* A host can compose local bytes with deferred durability without POSIX. */
struct persistence_fixture {
	const struct mh_user_data	*local, *durable;
	enum mh_storage_operation	operation;
	unsigned int			reads, writes, commits, cancellations;
	int	prepared, pending, fail_commit, fail_write;
};

static int
check_memory_values(void)
{
	const struct mh_user_data *first_data, *second_data;
	struct mh_user_data binding, before;
	struct mh_user_data_ops broken;
	struct mh_file first, second, untouched;
	char key[] = "same-key", bytes[] = "first";
	unsigned char sentinel;

	CHECK(mh_memory_user_data_open(&first_data, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&second_data, NULL) == MH_ERROR_NONE);
	before = binding = *first_data;
	broken = *first_data->ops;
	broken.replace = NULL;
	CHECK(mh_user_data_init(&binding, &broken, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(binding.ops == before.ops && binding.context == before.context);
	CHECK(mh_user_data_replace(first_data, key, bytes, 5, NULL) ==
		MH_ERROR_NONE);
	key[0] = bytes[0] = '!';
	CHECK(mh_user_data_replace(second_data, "same-key", "second", 6,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_read(first_data, "same-key", 5, &first, NULL) ==
		MH_ERROR_NONE);
	CHECK(first.size == 5 && memcmp(first.data, "first", 5) == 0);
	CHECK(mh_user_data_read(second_data, "same-key", 6, &second, NULL) ==
		MH_ERROR_NONE);
	CHECK(second.size == 6 && memcmp(second.data, "second", 6) == 0);
	free(second.data);

	untouched.data = &sentinel;
	untouched.size = 73;
	CHECK(mh_user_data_read(first_data, "same-key", 4, &untouched, NULL) ==
		MH_ERROR_RANGE);
	CHECK(mh_user_data_read(first_data, "SAME-KEY", 9, &untouched, NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(untouched.data == &sentinel && untouched.size == 73);
	CHECK(mh_user_data_replace(first_data, "same-key", NULL, 1, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_user_data_replace(first_data, "", "x", 1, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_user_data_read(first_data, "same-key", (size_t)-1, &untouched,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_user_data_read(first_data, "same-key", 5, &second, NULL) ==
		MH_ERROR_NONE);
	CHECK(second.size == 5 && memcmp(second.data, "first", 5) == 0);
	free(second.data);

	CHECK(mh_user_data_replace(first_data, "empty", NULL, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(first_data, "empty", 0, &second, NULL) ==
		MH_ERROR_NONE);
	CHECK(second.data != NULL && second.size == 0);
	free(second.data);
	CHECK(mh_user_data_replace(first_data, "same-key", "new", 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(first_data, "same-key", 3, &second, NULL) ==
		MH_ERROR_NONE);
	CHECK(second.size == 3 && memcmp(second.data, "new", 3) == 0);
	free(second.data);
	mh_memory_user_data_close(first_data);
	mh_memory_user_data_close(second_data);
	CHECK(first.size == 5 && memcmp(first.data, "first", 5) == 0);
	free(first.data);

	return 1;
}

static int
check_settings_values(void)
{
	static const char malformed[] =
		"PlayerName Changed\nCarHorn ABCD\nSFXVolume 1e99999\n";
	const struct mh_user_data *data;
	struct mh_settings settings, before, loaded;
	struct mh_file file, previous;
	unsigned long random;
	int present;

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_init(&settings, "Memory driver", 255, 87, 0, NULL) ==
		MH_ERROR_NONE);
	before = settings;
	random = 17;
	present = 71;
	CHECK(mh_settings_file_load_from(data, SETTINGS_KEY, &settings_catalog,
		&random, &settings, &present, NULL) == MH_ERROR_NONE);
	CHECK(present == 0 && random == 17 &&
		memcmp(&settings, &before, sizeof(settings)) == 0);
	CHECK(mh_settings_file_save_to(data, SETTINGS_KEY, &settings_catalog,
		&settings, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, SETTINGS_KEY, MH_SETTINGS_TEXT_CAPACITY,
		&previous, NULL) == MH_ERROR_NONE);
	settings.av.detail_mode = 5;
	CHECK(mh_settings_file_save_to(data, SETTINGS_KEY, &settings_catalog,
		&settings, NULL) == MH_ERROR_INVALID);
	CHECK(mh_user_data_read(data, SETTINGS_KEY, MH_SETTINGS_TEXT_CAPACITY,
		&file, NULL) == MH_ERROR_NONE);
	CHECK(file.size == previous.size &&
		memcmp(file.data, previous.data, file.size) == 0);
	free(file.data);
	free(previous.data);

	CHECK(mh_settings_init(&loaded, "Other driver", 0, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_settings_file_load_from(data, SETTINGS_KEY, &settings_catalog,
		&random, &loaded, &present, NULL) == MH_ERROR_NONE);
	CHECK(present == 1 && strcmp(loaded.player_name, "Memory driver") == 0);
	CHECK(loaded.player_color == before.player_color &&
		loaded.car_colors[0] == before.car_colors[0]);
	CHECK(mh_user_data_replace(data, SETTINGS_KEY, malformed,
		sizeof(malformed) - 1, NULL) == MH_ERROR_NONE);
	settings = before;
	random = 17;
	present = 71;
	CHECK(mh_settings_file_load_from(data, SETTINGS_KEY, &settings_catalog,
		&random, &settings, &present, NULL) != MH_ERROR_NONE);
	CHECK(present == 71 && random == 17 &&
		memcmp(&settings, &before, sizeof(settings)) == 0);
	mh_memory_user_data_close(data);

	return 1;
}

static enum mh_error_code
read_local(void *context, const char *key, size_t limit, struct mh_file *output,
	struct mh_error *error_out)
{
	struct persistence_fixture *fixture;

	fixture = context;

	if (!fixture->prepared)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "user data is not prepared");

	fixture->reads++;

	return mh_user_data_read(fixture->local, key, limit, output, error_out);
}

static enum mh_error_code
write_local(void *context, const char *key, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct persistence_fixture *fixture;

	fixture = context;

	if (!fixture->prepared)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "user data is not prepared");

	fixture->writes++;

	if (fixture->fail_write)
		return mh_error_set(
			error_out, MH_ERROR_IO, "injected local write failure");

	return mh_user_data_replace(
		fixture->local, key, bytes, size, error_out);
}

static enum mh_storage_status
begin_persistence(void *context, enum mh_storage_operation operation,
	struct mh_error *error_out)
{
	struct persistence_fixture *fixture;

	fixture = context;
	fixture->operation = operation;
	fixture->pending = 1;
	mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return MH_STORAGE_PENDING;
}

static enum mh_storage_status
poll_persistence(void *context, struct mh_error *error_out)
{
	struct persistence_fixture *fixture;
	struct mh_file bytes;
	enum mh_error_code status;

	fixture = context;
	fixture->pending = 0;

	if (fixture->operation == MH_STORAGE_PREPARE) {
		fixture->prepared = 1;
		mh_error_set(error_out, MH_ERROR_NONE, NULL);
		return MH_STORAGE_READY;
	}

	fixture->commits++;

	if (fixture->fail_commit) {
		mh_error_set(error_out, MH_ERROR_IO,
			"injected durable commit failure");
		return MH_STORAGE_FAILED;
	}

	status = mh_user_data_read(fixture->local, SETTINGS_KEY,
		MH_SETTINGS_TEXT_CAPACITY, &bytes, error_out);

	if (status != MH_ERROR_NONE)
		return MH_STORAGE_FAILED;

	status = mh_user_data_replace(fixture->durable, SETTINGS_KEY,
		bytes.data, bytes.size, error_out);
	free(bytes.data);

	return status == MH_ERROR_NONE ? MH_STORAGE_READY : MH_STORAGE_FAILED;
}

static void
cancel_persistence(void *context)
{
	struct persistence_fixture *fixture;

	fixture = context;
	fixture->pending = 0;
	fixture->cancellations++;
}

static int
check_settings_persistence(void)
{
	static const struct mh_user_data_ops data_ops = {
		read_local, write_local, NULL, NULL, NULL, NULL, NULL, NULL};

	static const struct mh_storage_ops storage_ops = {
		begin_persistence, poll_persistence, cancel_persistence};

	struct persistence_fixture fixture;
	struct mh_user_data view;
	struct mh_storage *storage;
	struct mh_settings settings, loaded;
	struct mh_file file;
	unsigned long random;
	int present;

	memset(&fixture, 0, sizeof(fixture));
	CHECK(mh_memory_user_data_open(&fixture.local, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&fixture.durable, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_init(&view, &data_ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_storage_create(&storage_ops, &fixture, &storage, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_settings_init(&settings, "First", 0, 0, 0, NULL) ==
		MH_ERROR_NONE);
	loaded = settings;
	random = 17;
	present = 71;
	CHECK(mh_settings_file_load_from(&view, SETTINGS_KEY, &settings_catalog,
		&random, &loaded, &present, NULL) == MH_ERROR_STATE);
	CHECK(fixture.reads == 0 && present == 71 && random == 17);
	CHECK(mh_storage_begin(storage, MH_STORAGE_PREPARE, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_storage_status(storage) == MH_STORAGE_PENDING &&
		!fixture.prepared);
	CHECK(mh_storage_step(storage) == MH_STORAGE_READY && fixture.prepared);
	CHECK(mh_settings_file_save_to(&view, SETTINGS_KEY, &settings_catalog,
		&settings, NULL) == MH_ERROR_NONE);
	CHECK(fixture.writes == 1);
	fixture.fail_commit = 1;
	CHECK(mh_storage_begin(storage, MH_STORAGE_COMMIT, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_storage_step(storage) == MH_STORAGE_FAILED);
	CHECK(mh_storage_error(storage, NULL) == MH_ERROR_IO);
	CHECK(mh_user_data_read(fixture.durable, SETTINGS_KEY,
		MH_SETTINGS_TEXT_CAPACITY, &file,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(mh_settings_file_load_from(&view, SETTINGS_KEY, &settings_catalog,
		&random, &loaded, &present, NULL) == MH_ERROR_NONE);
	CHECK(present == 1 && strcmp(loaded.player_name, "First") == 0);
	fixture.fail_commit = 0;
	CHECK(mh_storage_begin(storage, MH_STORAGE_COMMIT, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_storage_step(storage) == MH_STORAGE_READY);
	CHECK(fixture.writes == 1 && fixture.commits == 2);
	CHECK(mh_settings_file_load_from(fixture.durable, SETTINGS_KEY,
		&settings_catalog, &random, &loaded, &present,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(loaded.player_name, "First") == 0);

	strcpy(settings.player_name, "Second");
	fixture.fail_write = 1;
	CHECK(mh_settings_file_save_to(&view, SETTINGS_KEY, &settings_catalog,
		&settings, NULL) == MH_ERROR_IO);
	CHECK(mh_settings_file_load_from(&view, SETTINGS_KEY, &settings_catalog,
		&random, &loaded, &present, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(loaded.player_name, "First") == 0);
	fixture.fail_write = 0;
	CHECK(mh_settings_file_save_to(&view, SETTINGS_KEY, &settings_catalog,
		&settings, NULL) == MH_ERROR_NONE);
	CHECK(mh_storage_begin(storage, MH_STORAGE_COMMIT, NULL) ==
		MH_ERROR_NONE);
	mh_storage_close(storage);
	CHECK(fixture.cancellations == 1 && !fixture.pending);
	CHECK(mh_settings_file_load_from(&view, SETTINGS_KEY, &settings_catalog,
		&random, &loaded, &present, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(loaded.player_name, "Second") == 0);
	CHECK(mh_settings_file_load_from(fixture.durable, SETTINGS_KEY,
		&settings_catalog, &random, &loaded, &present,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(loaded.player_name, "First") == 0);
	mh_memory_user_data_close(fixture.local);
	mh_memory_user_data_close(fixture.durable);

	return 1;
}

int
main(void)
{
	if (!check_memory_values() || !check_settings_values() ||
		!check_settings_persistence())
		return 1;

	puts("User data: independent views, owned reads, settings transactions, deferred commit/retry/cancel passed");

	return 0;
}
