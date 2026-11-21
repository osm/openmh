#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/game/league_catalog.h"
#include "engine/game/league_names.h"
#include "engine/storage/path_names.h"

#define SAVED_SELECTION_SIZE 64

static int
text_terminated(const char *text, unsigned int limit)
{
	unsigned int i;

	if (text == NULL)
		return 0;

	for (i = 0; i < limit; i++)
		if (text[i] == 0)
			return 1;

	return 0;
}

static int
valid_catalog(const struct mh_league_catalog *catalog)
{
	unsigned int i;

	if (catalog == NULL || catalog->count > MH_LEAGUE_CATALOG_MAX_ENTRIES ||
		(catalog->count == 0 ? catalog->selected != 0
				: catalog->selected >= catalog->count) ||
		(catalog->count != 0 && catalog->entries == NULL))
		return 0;

	for (i = 0; i < catalog->count; i++)
		if (catalog->entries[i].dirty > 1 ||
			!text_terminated(catalog->entries[i].filename,
				MH_LEAGUE_CATALOG_FILENAME_SIZE) ||
			!text_terminated(catalog->entries[i].document.name,
				MH_LEAGUE_NAME_SIZE))
			return 0;

	return 1;
}

static enum mh_error_code
validate_document(
	const struct mh_league_document *document, struct mh_error *error_out)
{
	unsigned char *text;
	size_t size;
	enum mh_error_code status;

	if (document == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing League document");

	status = mh_league_save_text(&document->league, document->profiles,
		MH_LEAGUE_ENTRIES, document->name, document->created,
		document->races, document->laps, &text, &size, error_out);

	if (status == MH_ERROR_NONE)
		free(text);

	return status;
}

void
mh_league_catalog_close(struct mh_league_catalog *catalog)
{
	if (catalog == NULL)
		return;

	free(catalog->entries);
	memset(catalog, 0, sizeof(*catalog));
}

static struct mh_league_catalog_entry *
allocate_entries(unsigned int count)
{
	if (count == 0 || count > MH_LEAGUE_CATALOG_MAX_ENTRIES ||
		sizeof(struct mh_league_catalog_entry) > (size_t)-1 / count)
		return NULL;

	return (struct mh_league_catalog_entry *)calloc(
		count, sizeof(struct mh_league_catalog_entry));
}

enum mh_error_code
mh_league_catalog_copy(const struct mh_league_catalog *source,
	struct mh_league_catalog *output, struct mh_error *error_out)
{
	struct mh_league_catalog next;

	if (!valid_catalog(source) || output == NULL ||
		output->entries != NULL || output->count != 0 ||
		output->selected != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League catalog copy");

	next = *source;
	next.entries = NULL;

	if (source->count != 0) {
		next.entries = allocate_entries(source->count);

		if (next.entries == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory copying League catalog");

		memcpy(next.entries, source->entries,
			source->count * sizeof(*next.entries));
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_catalog_append(struct mh_league_catalog *catalog,
	const struct mh_league_document *document, const char *filename,
	struct mh_error *error_out)
{
	struct mh_league_catalog_entry *entries;
	enum mh_error_code status;
	unsigned int i;

	if (!valid_catalog(catalog) ||
		catalog->count == MH_LEAGUE_CATALOG_MAX_ENTRIES ||
		!text_terminated(filename, MH_LEAGUE_CATALOG_FILENAME_SIZE) ||
		filename[0] == 0 || strpbrk(filename, "/\\:") != NULL ||
		strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League catalog insertion");

	for (i = 0; i < catalog->count; i++)
		if (mh_ascii_equal(filename, catalog->entries[i].filename))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"ambiguous League filename");

	status = validate_document(document, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	entries = allocate_entries(catalog->count + 1);

	if (entries == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory extending League catalog");

	if (catalog->count != 0)
		memcpy(entries, catalog->entries,
			catalog->count * sizeof(*entries));

	entries[catalog->count].document = *document;
	strcpy(entries[catalog->count].filename, filename);
	free(catalog->entries);
	catalog->entries = entries;
	catalog->count++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_catalog_restore(struct mh_league_catalog *catalog, const char *name,
	struct mh_error *error_out)
{
	unsigned int i, selected;

	if (!valid_catalog(catalog) ||
		!text_terminated(name, SAVED_SELECTION_SIZE))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League catalog selection");

	selected = 0;

	for (i = 0; i < catalog->count; i++)
		if (mh_ascii_equal(name, catalog->entries[i].document.name))
			selected = i;

	catalog->selected = selected;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
create_entry(const struct mh_league_creation *creation, const char *name,
	unsigned long *random_state, struct mh_league_catalog_entry *output,
	struct mh_error *error_out)
{
	struct mh_league_catalog_entry entry;
	enum mh_error_code status;
	unsigned long next_random_state;

	if (creation == NULL || random_state == NULL ||
		*random_state > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League catalog creation");

	next_random_state = *random_state;
	memset(&entry, 0, sizeof(entry));
	status = mh_league_create(creation->profiles, creation->count, name,
		creation->player, creation->car, creation->color,
		creation->date, &next_random_state, &entry.document, error_out);

	if (status == MH_ERROR_NONE)
		status = validate_document(&entry.document, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	strcpy(entry.filename, entry.document.name);
	strcat(entry.filename, MH_USER_LEAGUE_EXTENSION);
	mh_ascii_lower(entry.filename);
	entry.dirty = 1;
	*output = entry;
	*random_state = next_random_state;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_catalog_create(struct mh_league_catalog *catalog,
	const struct mh_league_creation *creation, const char *name,
	unsigned long *random_state, struct mh_error *error_out)
{
	struct mh_league_catalog_entry *entries;
	enum mh_error_code status;
	unsigned long next_random_state;

	if (!valid_catalog(catalog) ||
		catalog->count == MH_LEAGUE_CATALOG_MAX_ENTRIES ||
		random_state == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League catalog creation");

	entries = allocate_entries(catalog->count + 1);

	if (entries == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating League");

	next_random_state = *random_state;
	status = create_entry(
		creation, name, &next_random_state, entries, error_out);

	if (status != MH_ERROR_NONE) {
		free(entries);
		return status;
	}

	if (catalog->count != 0)
		memcpy(entries + 1, catalog->entries,
			catalog->count * sizeof(*entries));

	free(catalog->entries);
	catalog->entries = entries;
	catalog->count++;
	catalog->selected = 0;
	*random_state = next_random_state;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_catalog_assign_name(struct mh_league_catalog *catalog,
	const unsigned char *name, struct mh_error *error_out)
{
	struct mh_league_catalog_entry next;
	const char **other_filenames;
	char filename[MH_LEAGUE_FILENAME_SIZE];
	enum mh_error_code status;
	unsigned int i;

	if (!valid_catalog(catalog) || catalog->count == 0 ||
		!text_terminated((const char *)name, MH_LEAGUE_NAME_SIZE))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League name assignment");

	next = catalog->entries[catalog->selected];

	if (strcmp(next.document.name, (const char *)name) != 0)
		next.dirty = 1;

	strcpy(next.document.name, (const char *)name);
	status = validate_document(&next.document, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	other_filenames = (const char **)malloc(
		catalog->count * sizeof(*other_filenames));

	if (other_filenames == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory assigning League filename");

	for (i = 0; i < catalog->count; i++)
		other_filenames[i] = i == catalog->selected
			? NULL
			: catalog->entries[i].filename;

	status = mh_league_filename(
		name, other_filenames, catalog->count, filename, error_out);
	free(other_filenames);

	if (status != MH_ERROR_NONE)
		return status;

	strcpy(next.filename, filename);
	catalog->entries[catalog->selected] = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_catalog_remove(struct mh_league_catalog *catalog, unsigned int index,
	const struct mh_league_creation *creation, unsigned long *random_state,
	struct mh_error *error_out)
{
	struct mh_league_catalog_entry replacement;
	enum mh_error_code status;

	if (!valid_catalog(catalog) || index >= catalog->count)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid League removal");

	if (catalog->count == 1) {
		status = create_entry(creation, "Default", random_state,
			&replacement, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		catalog->entries[0] = replacement;
		catalog->selected = 0;
	} else {
		if (index + 1 < catalog->count)
			memmove(catalog->entries + index,
				catalog->entries + index + 1,
				(catalog->count - index - 1) *
					sizeof(*catalog->entries));

		catalog->count--;

		if (catalog->selected >= catalog->count)
			catalog->selected = catalog->count - 1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
