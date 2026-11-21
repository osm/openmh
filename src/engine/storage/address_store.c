#include <stdlib.h>
#include <string.h>

#include "engine/core/text.h"
#include "engine/storage/address_store.h"
#include "engine/storage/path_names.h"
#include "interfaces/storage/data_directory.h"

#define FILE_SIZE_LIMIT 1048576UL
#define BOOK_TEXT_LIMIT 33554432UL

struct address_file {
	char			filename[MH_ADDRESS_FILENAME_BYTES];
	struct mh_data_file	*identity;
	struct mh_file		bytes;
};

struct mh_address_store {
	struct mh_address_book		book;
	struct mh_data_file		**identities;
	struct mh_data_directory	*directory;
};

struct address_scan {
	struct address_file	*files;
	unsigned int		count;
	struct mh_error		error;
};

struct publication {
	struct address_file	*files;
	const char		*filename;
};

static int
compare_filenames(const void *left, const void *right)
{
	const struct address_file *first, *second;

	first = left;
	second = right;

	return mh_ascii_compare(first->filename, second->filename);
}

static void
sort_addresses(struct mh_address_store *store)
{
	struct mh_data_file *identities[MH_ADDRESS_LIMIT];
	unsigned int previous_indices[MH_ADDRESS_LIMIT], i;

	memcpy(identities, store->identities,
		store->book.count * sizeof(*identities));
	mh_address_book_sort(&store->book, previous_indices);

	for (i = 0; i < store->book.count; i++)
		store->identities[i] = identities[previous_indices[i]];
}

static enum mh_directory_action
visit_file(void *context, const char *name)
{
	struct address_scan *scan;
	size_t length;

	scan = context;
	length = strlen(name);

	if (length < (sizeof(MH_USER_ADDRESS_EXTENSION) - 1) ||
		mh_ascii_compare(
			name + length - (sizeof(MH_USER_ADDRESS_EXTENSION) - 1),
			MH_USER_ADDRESS_EXTENSION) != 0)
		return MH_DIRECTORY_CONTINUE;

	if (scan->count == MH_ADDRESS_LIMIT - 1 ||
		length >= MH_ADDRESS_FILENAME_BYTES ||
		length == (sizeof(MH_USER_ADDRESS_EXTENSION) - 1) ||
		strpbrk(name, "/\\:") != NULL) {
		mh_error_set(&scan->error, MH_ERROR_RANGE,
			"address book filenames exceed native bounds");
		return MH_DIRECTORY_STOP;
	}

	strcpy(scan->files[scan->count++].filename, name);

	return MH_DIRECTORY_CONTINUE;
}

static enum mh_error_code
scan_files(struct mh_data_directory *directory, struct address_file *files,
	unsigned int *count, struct mh_error *error_out)
{
	struct address_scan scan;
	enum mh_error_code status;
	unsigned int i;

	memset(&scan, 0, sizeof(scan));
	scan.files = files;
	status =
		mh_data_directory_list(directory, visit_file, &scan, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (scan.error.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &scan.error);

	qsort(files, scan.count, sizeof(*files), compare_filenames);

	for (i = 1; i < scan.count; i++)
		if (mh_ascii_compare(
			files[i - 1].filename, files[i].filename) == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"ambiguous NPC filenames");

	*count = scan.count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_address_store_close(struct mh_address_store *store)
{
	unsigned int i;

	if (store == NULL)
		return;

	if (store->identities != NULL)
		for (i = 0; i < MH_ADDRESS_LIMIT; i++)
			mh_data_file_close(store->identities[i]);

	mh_address_book_free(&store->book);
	free(store->identities);
	mh_data_directory_close(store->directory);
	free(store);
}

const struct mh_address_book *
mh_address_store_book(const struct mh_address_store *store)
{
	return store == NULL ? NULL : &store->book;
}

static void
adopt_file_identities(struct mh_address_store *store,
	struct address_file *files, unsigned int count)
{
	unsigned int file_index, entry_index;

	/* Entry zero is built in; the parser retains only TCP file entries. */
	entry_index = 1;

	for (file_index = 0;
		file_index < count && entry_index < store->book.count;
		file_index++)
		if (strcmp(files[file_index].filename,
			store->book.entries[entry_index].filename) == 0) {
			store->identities[entry_index++] =
				files[file_index].identity;
			files[file_index].identity = NULL;
		}
}

enum mh_error_code
mh_address_store_open_in(struct mh_data_directory *directory,
	struct mh_address_store **output, struct mh_error *error_out)
{
	struct mh_address_store *store;
	struct address_file *files;
	struct mh_address_source *sources;
	enum mh_error_code status;
	unsigned int count, i;
	size_t total_size, limit;

	if (directory == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid address book directory");

	store = calloc(1, sizeof(*store));

	if (store == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening address book");

	store->identities =
		calloc(MH_ADDRESS_LIMIT, sizeof(*store->identities));
	files = calloc(MH_ADDRESS_LIMIT, sizeof(*files));
	sources = calloc(MH_ADDRESS_LIMIT, sizeof(*sources));
	status = store->identities == NULL || files == NULL || sources == NULL
		? mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory loading address book")
		: MH_ERROR_NONE;

	count = 0;

	if (status == MH_ERROR_NONE)
		status = scan_files(directory, files, &count, error_out);

	total_size = 0;

	for (i = 0; status == MH_ERROR_NONE && i < count; i++) {
		limit = BOOK_TEXT_LIMIT - total_size;

		if (limit > FILE_SIZE_LIMIT)
			limit = FILE_SIZE_LIMIT;

		status = mh_data_directory_read(directory, files[i].filename,
			limit, &files[i].bytes, &files[i].identity, error_out);
		sources[i].filename = files[i].filename;
		sources[i].text = files[i].bytes.data;
		sources[i].size = files[i].bytes.size;
		total_size += files[i].bytes.size;
	}

	if (status == MH_ERROR_NONE)
		status = mh_address_book_read(
			sources, count, &store->book, error_out);

	if (status == MH_ERROR_NONE)
		adopt_file_identities(store, files, count);

	if (files != NULL)
		for (i = 0; i < count; i++) {
			free(files[i].bytes.data);
			mh_data_file_close(files[i].identity);
		}

	free(files);
	free(sources);

	if (status != MH_ERROR_NONE) {
		mh_address_store_close(store);
		return status;
	}

	store->directory = directory;
	sort_addresses(store);
	*output = store;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_address_store_reload(
	struct mh_address_store *store, struct mh_error *error_out)
{
	struct mh_address_store *next, previous;
	enum mh_error_code status;

	if (store == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing address book directory");

	status = mh_address_store_open_in(store->directory, &next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	previous = *store;
	*store = *next;
	*next = previous;

	/* The refreshed catalog retains the same held directory. */
	next->directory = NULL;
	mh_address_store_close(next);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_publication(void *context, struct mh_data_directory *directory,
	struct mh_error *error_out)
{
	struct publication *publication;
	enum mh_error_code status;
	unsigned int count, i;

	publication = context;
	status = scan_files(directory, publication->files, &count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (count == MH_ADDRESS_LIMIT - 1)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"address book directory became full");

	for (i = 0; i < count; i++)
		if (mh_ascii_compare(publication->files[i].filename,
			publication->filename) == 0)
			return mh_error_set(error_out, MH_ERROR_EXISTS,
				"NPC destination appeared while saving");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_address_store_add(struct mh_address_store *store, const char *name,
	const char *ip, struct mh_error *error_out)
{
	struct mh_address_book next;
	struct address_file *files;
	const char **names;
	enum mh_error_code status;
	struct mh_data_file *created;
	struct publication publication;
	char text[MH_ADDRESS_TEXT_CAPACITY];
	unsigned int count, i;
	size_t size;

	status = mh_data_directory_validate(
		store == NULL ? NULL : store->directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));
	next.entries = (struct mh_address_entry *)malloc(
		store->book.count * sizeof(*next.entries));
	files = (struct address_file *)calloc(MH_ADDRESS_LIMIT, sizeof(*files));
	names = (const char **)malloc(MH_ADDRESS_LIMIT * sizeof(*names));

	if (next.entries == NULL || files == NULL || names == NULL)
		status = mh_error_set(
			error_out, MH_ERROR_MEMORY, "out of memory adding NPC");

	if (status == MH_ERROR_NONE) {
		next.count = store->book.count;
		memcpy(next.entries, store->book.entries,
			next.count * sizeof(*next.entries));
		status = mh_address_book_add(&next, name, ip, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = scan_files(store->directory, files, &count, error_out);

	if (status == MH_ERROR_NONE && count == MH_ADDRESS_LIMIT - 1)
		status = mh_error_set(error_out, MH_ERROR_RANGE,
			"address book directory is full");

	if (status == MH_ERROR_NONE) {
		for (i = 0; i < count; i++)
			names[i] = files[i].filename;

		status = mh_address_filename(
			next.entries[next.count - 1].profile.name, names, count,
			next.entries[next.count - 1].filename, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_address_profile_encode(
			&next.entries[next.count - 1].profile, text,
			sizeof(text), &size, error_out);

	if (status == MH_ERROR_NONE) {
		publication.files = files;
		publication.filename = next.entries[next.count - 1].filename;
		status = mh_data_directory_create(store->directory,
			publication.filename, text, size, check_publication,
			&publication, &created, error_out);

		if (status == MH_ERROR_NONE) {
			store->identities[next.count - 1] = created;
			mh_address_book_free(&store->book);
			store->book = next;
			sort_addresses(store);
			memset(&next, 0, sizeof(next));
		}
	}

	free(files);
	free(names);
	mh_address_book_free(&next);

	return status;
}

enum mh_error_code
mh_address_store_remove(struct mh_address_store *store, unsigned int index,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_data_directory_validate(
		store == NULL ? NULL : store->directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (index == 0 || index >= store->book.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or protected NPC removal");

	status = mh_data_file_remove(store->identities[index], error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_data_file_close(store->identities[index]);
	memmove(store->identities + index, store->identities + index + 1,
		(store->book.count - index - 1) * sizeof(*store->identities));
	store->identities[store->book.count - 1] = NULL;

	return mh_address_book_remove(&store->book, index, error_out);
}
