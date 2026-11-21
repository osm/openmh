#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data.h"
#include "engine/storage/address_store.h"
#include "interfaces/storage/data_directory_backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "address provider:%d: %s\n", __LINE__, \
				#x);                                           \
			return 0;                                              \
		}                                                              \
	} while (0)

static int
put(const struct mh_user_data *data, const char *key, const char *text)
{
	return mh_user_data_replace(data, key, text, strlen(text), NULL) ==
		MH_ERROR_NONE;
}

static int
remove_named(struct mh_data_directory *directory, const char *name)
{
	struct mh_file bytes;
	struct mh_data_file *file;

	CHECK(mh_data_directory_read(directory, name, 1024, &bytes, &file,
		NULL) == MH_ERROR_NONE);
	free(bytes.data);
	CHECK(mh_data_file_remove(file, NULL) == MH_ERROR_NONE);
	mh_data_file_close(file);

	return 1;
}

static enum mh_error_code
reject_publication(void *context, struct mh_data_directory *directory,
	struct mh_error *error_out)
{
	int *calls;

	(void)directory;
	calls = context;
	(*calls)++;

	return mh_error_set(
		error_out, MH_ERROR_RANGE, "injected catalog refusal");
}

static int
file_identities(void)
{
	const struct mh_user_data *data;
	struct mh_data_directory *directory, *other;
	struct mh_data_file *created, *loaded, *untouched;
	struct mh_file bytes, snapshot;
	unsigned char sentinel;
	int calls;

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(data, "Book", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "Book", "installed-assets",
		&directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "Book", "installed-assets",
		&other, NULL) == MH_ERROR_NONE);
	created = NULL;
	calls = 0;
	CHECK(mh_data_directory_create(directory, "Refused.NPC", "abc", 3,
		reject_publication, &calls, &created,
		NULL) == MH_ERROR_RANGE);
	CHECK(calls == 1 && created == NULL);
	CHECK(mh_user_data_read(data, "Book/Refused.NPC", 3, &bytes, NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(mh_data_directory_create(directory, "Saved.NPC", "abc", 3, NULL,
		NULL, &created, NULL) == MH_ERROR_NONE);
	untouched = created;
	CHECK(mh_data_directory_create(other, "Saved.NPC", "bad", 3, NULL, NULL,
		&untouched, NULL) == MH_ERROR_EXISTS);
	CHECK(untouched == created);
	bytes.data = &sentinel;
	bytes.size = 19;
	CHECK(mh_data_directory_read(other, "Saved.NPC", 2, &bytes, &untouched,
		NULL) == MH_ERROR_RANGE);
	CHECK(bytes.data == &sentinel && bytes.size == 19 &&
		untouched == created);
	CHECK(mh_data_directory_read(other, "Saved.NPC", 3, &snapshot, &loaded,
		NULL) == MH_ERROR_NONE);
	CHECK(snapshot.size == 3 && memcmp(snapshot.data, "abc", 3) == 0);

	/*
	 * Even an identical replacement invalidates previously loaded
	 * identities.
	 */
	CHECK(put(data, "Book/Saved.NPC", "abc"));
	CHECK(mh_data_file_remove(created, NULL) == MH_ERROR_STATE);
	CHECK(mh_data_file_remove(loaded, NULL) == MH_ERROR_STATE);
	mh_data_file_close(created);
	mh_data_file_close(loaded);
	CHECK(remove_named(directory, "Saved.NPC"));
	CHECK(mh_data_directory_create(other, "Saved.NPC", NULL, 0, NULL, NULL,
		&created, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_read(directory, "Saved.NPC", 0, &bytes, &loaded,
		NULL) == MH_ERROR_NONE);
	CHECK(bytes.data != NULL && bytes.size == 0);
	free(bytes.data);
	CHECK(mh_data_file_remove(created, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_remove(loaded, NULL) == MH_ERROR_STATE);
	mh_data_file_close(created);
	mh_data_file_close(loaded);
	mh_data_directory_close(directory);
	mh_data_directory_close(other);
	mh_memory_user_data_close(data);
	CHECK(memcmp(snapshot.data, "abc", 3) == 0);
	free(snapshot.data);

	return 1;
}

static int
memory_addresses(void)
{
	static const char changed[] =
		"Type TCP\nAddress changed.example\nDescription Changed\n";
	const struct mh_user_data *data, *separate;
	struct mh_data_directory *directory, *editor;
	struct mh_address_store *store, *other, *untouched;
	const struct mh_address_book *book;
	struct mh_address_entry snapshot[4];
	struct mh_file bytes;
	char malformed[256];

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&separate, NULL) == MH_ERROR_NONE);
	CHECK(put(data, "Book/Alpha.NPC", "Type IPX\n"));
	CHECK(put(data, "Book/beta.npc", "Type TCP\nAddress host.example\n"));
	CHECK(mh_user_data_open_directory(data, "Book", "installed-assets",
		&directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "Book", "installed-assets",
		&editor, NULL) == MH_ERROR_NONE);
	CHECK(mh_address_store_open_in(directory, &store, NULL) ==
		MH_ERROR_NONE);
	book = mh_address_store_book(store);
	CHECK(book->count == 2 &&
		strcmp(book->entries[1].profile.name, "TCP/IP_03") == 0);
	CHECK(mh_address_store_add(store, "Alpha", "127.0.0.1", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_address_store_add(store, "Alpha", "game.example:23456",
		NULL) == MH_ERROR_NONE);
	CHECK(book->count == 4 &&
		strcmp(book->entries[1].filename, "alpha!.npc") == 0 &&
		strcmp(book->entries[2].filename, "alpha!!.npc") == 0);
	CHECK(mh_user_data_read(data, "Book/alpha!.npc", 512, &bytes, NULL) ==
		MH_ERROR_NONE);
	CHECK(bytes.size != 0);
	free(bytes.data);
	memcpy(snapshot, book->entries, sizeof(snapshot));
	CHECK(mh_address_store_remove(store, 0, NULL) == MH_ERROR_INVALID);
	CHECK(put(data, "Book/beta.npc", changed));
	CHECK(mh_address_store_remove(store, 3, NULL) == MH_ERROR_STATE);
	CHECK(book->count == 4 &&
		memcmp(snapshot, book->entries, sizeof(snapshot)) == 0);
	CHECK(mh_address_store_remove(store, 1, NULL) == MH_ERROR_NONE);
	CHECK(book->count == 3 &&
		strcmp(book->entries[1].filename, "alpha!!.npc") == 0);
	memcpy(snapshot, book->entries, 3 * sizeof(*snapshot));
	CHECK(put(data, "Book/ALPHA.npc", "Type TCP\n"));
	CHECK(mh_address_store_reload(store, NULL) == MH_ERROR_INVALID);
	CHECK(mh_address_store_add(store, "New", "ip", NULL) ==
		MH_ERROR_INVALID);
	CHECK(book->count == 3 &&
		memcmp(snapshot, book->entries, 3 * sizeof(*snapshot)) == 0);
	CHECK(remove_named(editor, "ALPHA.npc"));
	memset(malformed, 'x', sizeof(malformed));
	memcpy(malformed, "Type TCP\nAddress ", 17);
	malformed[sizeof(malformed) - 1] = 0;
	CHECK(put(data, "Book/Bad.NPC", malformed));
	CHECK(mh_address_store_reload(store, NULL) == MH_ERROR_RANGE);
	CHECK(book->count == 3 &&
		memcmp(snapshot, book->entries, 3 * sizeof(*snapshot)) == 0);
	CHECK(remove_named(editor, "Bad.NPC"));
	CHECK(mh_address_store_reload(store, NULL) == MH_ERROR_NONE);
	CHECK(book->count == 3 &&
		strcmp(book->entries[1].filename, "alpha!!.npc") == 0 &&
		strcmp(book->entries[2].profile.name, "Changed") == 0);
	CHECK(strcmp(book->entries[1].profile.ip, "game.example:23456") == 0);
	CHECK(mh_address_store_remove(store, 2, NULL) == MH_ERROR_NONE);
	CHECK(mh_address_store_remove(store, 1, NULL) == MH_ERROR_NONE);
	CHECK(book->count == 1);

	CHECK(mh_user_data_open_directory(separate, "", "installed-assets",
		&directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_address_store_open_in(directory, &other, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_address_store_add(other, "Alpha", "other.example", NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(mh_address_store_book(other)->entries[1].filename,
		"alpha.npc") == 0 &&
		book->count == 1);
	mh_address_store_close(other);
	mh_address_store_close(store);

	/*
	 * Failed open retains the caller's directory for correction and retry.
	 */
	CHECK(put(data, "Book/ALPHA.npc", "Type TCP\n"));
	untouched = NULL;
	CHECK(mh_address_store_open_in(editor, &untouched, NULL) ==
		MH_ERROR_INVALID);
	CHECK(untouched == NULL &&
		mh_data_directory_validate(editor, NULL) == MH_ERROR_NONE);
	CHECK(remove_named(editor, "ALPHA.npc"));
	CHECK(mh_address_store_open_in(editor, &store, NULL) == MH_ERROR_NONE);
	CHECK(mh_address_store_book(store)->count == 1);
	mh_address_store_close(store);
	mh_memory_user_data_close(data);
	mh_memory_user_data_close(separate);

	return 1;
}

int
main(void)
{
	if (!file_identities() || !memory_addresses())
		return 1;

	puts("Address provider: portable catalogs, owned identities, exclusive publication, stale deletion, reload rollback and isolated memory stores passed");

	return 0;
}
