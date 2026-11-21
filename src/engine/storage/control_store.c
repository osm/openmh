#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_catalog.h"
#include "engine/core/text.h"
#include "engine/input/keyboard.h"
#include "engine/storage/control_internal.h"
#include "engine/storage/path_names.h"

#define PROFILE_SIZE_LIMIT 1048576UL
#define CATALOG_TEXT_LIMIT 33554432UL

struct control_scan {
	struct control_file	*files;
	unsigned int		count, added;
	int			from_user;
	enum mh_error_code	status;
};

static int
compare_files(const void *left, const void *right)
{
	const struct control_file *first, *second;
	int result;

	first = (const struct control_file *)left;
	second = (const struct control_file *)right;
	result = mh_ascii_compare(first->filename, second->filename);

	return result != 0 ? result : first->from_user - second->from_user;
}

void
mh_control_store_close(struct mh_control_store *store)
{
	unsigned int i;

	if (store == NULL)
		return;

	for (i = 0; i < store->count; i++)
		mh_data_file_close(store->files[i].identity);

	mh_data_directory_close(store->directory);
	mh_data_directory_close(store->source_directory);
	free(store->source_path);
	free(store->files);
	free(store);
}

static enum mh_directory_action
collect_file(void *context, const char *name)
{
	struct control_scan *scan;
	size_t length;

	scan = context;
	length = strlen(name);

	if (length < (sizeof(MH_USER_CONTROL_EXTENSION) - 1) ||
		mh_ascii_compare(
			name + length - (sizeof(MH_USER_CONTROL_EXTENSION) - 1),
			MH_USER_CONTROL_EXTENSION) != 0)
		return MH_DIRECTORY_CONTINUE;

	if (length >= CONTROL_FILENAME_SIZE || strpbrk(name, "/\\:") != NULL ||
		scan->added == MH_CONTROL_PROFILES) {
		scan->status = MH_ERROR_RANGE;
		return MH_DIRECTORY_STOP;
	}

	strcpy(scan->files[scan->count].filename, name);
	scan->files[scan->count].from_user = scan->from_user;
	scan->count++;
	scan->added++;

	return MH_DIRECTORY_CONTINUE;
}

static enum mh_error_code
collect_files(struct mh_control_store *store, unsigned int *count,
	struct mh_error *error_out)
{
	struct control_scan scan;
	enum mh_error_code status;

	memset(&scan, 0, sizeof(scan));
	scan.files = store->files;
	status = MH_ERROR_NONE;

	if (store->source_directory != NULL)
		status = mh_data_directory_list(store->source_directory,
			collect_file, &scan, error_out);

	if (status == MH_ERROR_NONE && scan.status == MH_ERROR_NONE &&
		store->directory != NULL) {
		scan.from_user = 1;
		scan.added = 0;
		status = mh_data_directory_list(
			store->directory, collect_file, &scan, error_out);
	}

	if (scan.status != MH_ERROR_NONE)
		return mh_error_set(error_out, scan.status,
			"control profile filenames exceed native bounds");

	*count = scan.count;

	return status;
}

/* Usable keyboard controls when an installation supplies no CLO profiles. */
static enum mh_error_code
load_keyboard_default(
	struct mh_control_source *source, struct mh_error *error_out)
{
	static const char text[] = "LayoutName Keyboard\n";
	unsigned char *copy;

	copy = malloc(sizeof(text) - 1);

	if (copy == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating default keyboard profile");

	memcpy(copy, text, sizeof(text) - 1);
	source->text = copy;
	source->size = sizeof(text) - 1;
	source->present = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_profile(struct mh_control_store *store, struct control_file *file,
	struct mh_control_source *source, size_t *total_size,
	struct mh_error *error_out)
{
	struct mh_file bytes;
	enum mh_error_code status;
	struct mh_data_file *installed_file;
	size_t limit;

	if (file->builtin)
		return load_keyboard_default(source, error_out);

	limit = CATALOG_TEXT_LIMIT - *total_size;

	if (limit > PROFILE_SIZE_LIMIT)
		limit = PROFILE_SIZE_LIMIT;

	if (file->from_user) {
		status =
			mh_data_directory_read(store->directory, file->filename,
				limit, &bytes, &file->identity, error_out);
	} else {
		installed_file = NULL;
		status = mh_data_directory_read(store->source_directory,
			file->filename, limit, &bytes, &installed_file,
			error_out);
		mh_data_file_close(installed_file);
	}

	if (status == MH_ERROR_NOT_FOUND)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (status != MH_ERROR_NONE)
		return status;

	source->text = bytes.data;
	source->size = bytes.size;
	source->present = 1;
	*total_size += bytes.size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
merge_files(struct mh_control_store *store, unsigned int count,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int i, merged_count;

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	merged_count = 0;
	qsort(store->files, count, sizeof(*store->files), compare_files);

	for (i = 1; i < count; i++) {
		if (store->files[i - 1].from_user ==
				store->files[i].from_user &&
			mh_ascii_compare(store->files[i - 1].filename,
				store->files[i].filename) == 0) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"ambiguous control profile filenames");
			break;
		}
	}

	for (i = 0; status == MH_ERROR_NONE && i < count; i++) {
		if (i + 1 < count &&
			mh_ascii_compare(store->files[i].filename,
				store->files[i + 1].filename) == 0)
			continue;

		if (merged_count == MH_CONTROL_PROFILES) {
			status = mh_error_set(error_out, MH_ERROR_RANGE,
				"too many control profiles");
			break;
		}

		store->files[merged_count++] = store->files[i];
	}

	store->count = merged_count;

	if (status == MH_ERROR_NONE && merged_count == 0) {
		strcpy(store->files[0].filename,
			MH_ASSET_KEYBOARD_CONTROLS_NAME);
		store->files[0].builtin = 1;
		store->count = 1;
	}

	return status;
}

/* Adapt the shipped Keyboard preset; user profiles keep their own bindings. */
static void
apply_keyboard_defaults(
	const struct control_file *file, struct mh_control_layout *layout)
{
	if (file->from_user ||
		!mh_ascii_equal(
			file->filename, MH_ASSET_KEYBOARD_CONTROLS_NAME) ||
		layout->bindings[MH_CONTROL_HORN] != MH_KEY_SPACE ||
		layout->bindings[MH_CONTROL_HANDBRAKE] != MH_KEY_LSHIFT)
		return;

	layout->bindings[MH_CONTROL_HORN] = MH_KEY_LSHIFT;
	layout->bindings[MH_CONTROL_HANDBRAKE] = MH_KEY_SPACE;
}

enum mh_error_code
mh_control_store_open_from(const struct mh_resource_source *resources,
	const char *source_directory, struct mh_data_directory *directory,
	const char *selection, struct mh_control_catalog *catalog,
	struct mh_control_store **output, struct mh_error *error_out)
{
	struct mh_control_store *store;
	struct mh_control_catalog next;
	struct mh_control_source *sources;
	enum mh_error_code status;
	size_t total_size;
	unsigned int i, count;

	if (resources == NULL || source_directory == NULL || catalog == NULL ||
		output == NULL || catalog->profiles != NULL ||
		catalog->count != 0 || catalog->selected != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid control profile store input");

	store = calloc(1, sizeof(*store));

	if (store == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening control profiles");

	store->source_path = malloc(strlen(source_directory) + 1);
	store->files = calloc(2 * MH_CONTROL_PROFILES, sizeof(*store->files));

	if (store->source_path == NULL || store->files == NULL) {
		mh_control_store_close(store);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening control profiles");
	}

	strcpy(store->source_path, source_directory);
	store->resources = resources;
	store->directory = directory;
	sources = NULL;
	memset(&next, 0, sizeof(next));
	count = 0;
	total_size = 0;
	status = mh_resource_source_open_directory(resources, source_directory,
		&store->source_directory, error_out);

	if (status == MH_ERROR_NOT_FOUND)
		status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (status == MH_ERROR_NONE)
		status = collect_files(store, &count, error_out);

	if (status == MH_ERROR_NONE)
		status = merge_files(store, count, error_out);

	if (status == MH_ERROR_NONE) {
		sources = calloc(store->count, sizeof(*sources));

		if (sources == NULL)
			status = mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory reading control profiles");
	}

	for (i = 0; status == MH_ERROR_NONE && i < store->count; i++)
		status = load_profile(store, &store->files[i], &sources[i],
			&total_size, error_out);

	if (status == MH_ERROR_NONE && directory != NULL)
		status = mh_data_directory_validate(directory, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_control_catalog_read(sources, store->count,
			selection, NULL, &next, error_out);

	if (status == MH_ERROR_NONE)
		for (i = 0; i < store->count; i++) {
			apply_keyboard_defaults(
				&store->files[i], &next.profiles[i].layout);

			if (!store->files[i].from_user)
				mh_ascii_lower(store->files[i].filename);

			if (store->files[i].builtin)
				next.profiles[i].dirty = 1;
		}

	if (sources != NULL)
		for (i = 0; i < store->count; i++)
			free((void *)sources[i].text);

	free(sources);

	if (status != MH_ERROR_NONE) {
		/* Failed open leaves the writable directory with its caller. */
		store->directory = NULL;
		mh_control_store_close(store);
		return status;
	}

	*catalog = next;
	*output = store;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_control_store_reload(struct mh_control_store **store, const char *selection,
	struct mh_control_catalog *catalog, struct mh_error *error_out)
{
	struct mh_control_store *fresh;
	struct mh_control_catalog next;
	enum mh_error_code status;

	if (store == NULL || *store == NULL || catalog == NULL ||
		catalog->profiles == NULL ||
		catalog->count != (*store)->count ||
		catalog->selected >= catalog->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid control profile reload catalog");

	memset(&next, 0, sizeof(next));
	status = mh_control_store_open_from((*store)->resources,
		(*store)->source_path, (*store)->directory, selection, &next,
		&fresh, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	(*store)->directory = NULL;
	mh_control_store_close(*store);
	mh_control_catalog_free(catalog);
	*store = fresh;
	*catalog = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_control_store_restrict(struct mh_control_store *store,
	struct mh_control_catalog *catalog, const unsigned char *keep,
	struct mh_error *error_out)
{
	unsigned int i, count, selected;

	if (store == NULL || catalog == NULL || keep == NULL ||
		catalog->profiles == NULL || catalog->count != store->count ||
		catalog->selected >= catalog->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid control profile selection");

	count = 0;

	for (i = 0; i < catalog->count; i++)
		count += keep[i] != 0;

	if (count == 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"control profile selection is empty");

	count = selected = 0;

	for (i = 0; i < catalog->count; i++) {
		if (!keep[i]) {
			mh_data_file_close(store->files[i].identity);
			continue;
		}

		if (i == catalog->selected)
			selected = count;

		store->files[count] = store->files[i];
		catalog->profiles[count++] = catalog->profiles[i];
	}

	catalog->count = store->count = count;
	catalog->selected = selected;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
