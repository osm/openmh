#include <stdlib.h>
#include <string.h>

#include "backends/storage/zip_internal.h"
#include "engine/archive/zip_layout.h"
#include "engine/core/binary.h"
#include "engine/core/text.h"

/* Unix file-type bits stored in ZIP external attributes, on every host. */
#define ZIP_UNIX_TYPE_MASK 0170000UL
#define ZIP_UNIX_FILE 0100000UL
#define ZIP_UNIX_DIRECTORY 0040000UL

static enum mh_error_code
invalid(struct mh_error *error_out)
{
	return mh_error_set(error_out, MH_ERROR_INVALID, "invalid ZIP package");
}

int
mh_zip_compare_entries(const void *left, const void *right)
{
	const struct mh_zip_entry *a, *b;
	const unsigned char *x, *y;
	unsigned int first, second;

	a = left;
	b = right;
	x = (const unsigned char *)a->path;
	y = (const unsigned char *)b->path;

	/* Group each directory and its children before similar siblings. */
	do {
		first = *x++;
		second = *y++;

		if (first >= 'A' && first <= 'Z')
			first += 'a' - 'A';

		if (second >= 'A' && second <= 'Z')
			second += 'a' - 'A';

		if (first == '/')
			first = 1;

		if (second == '/')
			second = 1;

		if (first != second)
			return first < second ? -1 : 1;
	} while (first != 0);

	return 0;
}

void
mh_zip_index_free(struct mh_zip_resources *zip)
{
	size_t i;

	for (i = 0; i < zip->count; i++)
		free(zip->entries[i].path);

	free(zip->entries);
}

static enum mh_error_code
read_at(struct mh_zip_resources *zip, unsigned long offset, void *bytes,
	size_t size, struct mh_error *error_out)
{
	if (offset > zip->length || size > zip->length - offset)
		return invalid(error_out);

	return mh_resource_source_read_at(
		zip->source, zip->path, offset, bytes, size, error_out);
}

static enum mh_error_code
read_end(struct mh_zip_resources *zip, unsigned long *central_size,
	struct mh_error *error_out)
{
	unsigned char *tail, *end;
	size_t size, at;
	unsigned long count;
	enum mh_error_code status;

	if (zip->length < MH_ZIP_END_BYTES || zip->length >= MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"package must be a classic ZIP smaller than 4 GiB");

	size = zip->length > MH_ZIP_END_SEARCH_BYTES ? MH_ZIP_END_SEARCH_BYTES
						: (size_t)zip->length;
	tail = malloc(size);

	if (tail == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading ZIP index");

	status = read_at(zip, zip->length - size, tail, size, error_out);

	if (status != MH_ERROR_NONE) {
		free(tail);
		return status;
	}

	at = size - MH_ZIP_END_BYTES;

	for (;;) {
		end = tail + at;

		if (mh_read_u32le(end) == MH_ZIP_END_SIGNATURE &&
			mh_read_u16le(end + MH_ZIP_END_COMMENT_SIZE) ==
				size - at - MH_ZIP_END_BYTES)
			break;

		if (at == 0) {
			free(tail);
			return invalid(error_out);
		}

		at--;
	}

	count = mh_read_u16le(end + MH_ZIP_END_ENTRIES);
	zip->central = mh_read_u32le(end + MH_ZIP_END_CENTRAL_OFFSET);
	*central_size = mh_read_u32le(end + MH_ZIP_END_CENTRAL_SIZE);

	if (mh_read_u16le(end + MH_ZIP_END_DISK) != 0 ||
		mh_read_u16le(end + MH_ZIP_END_CENTRAL_DISK) != 0 ||
		count != mh_read_u16le(end + MH_ZIP_END_DISK_ENTRIES) ||
		count > MH_ZIP_ENTRY_MAX ||
		zip->central > zip->length - size + at ||
		*central_size != zip->length - size + at - zip->central) {
		free(tail);
		return invalid(error_out);
	}

	free(tail);

	if (count > *central_size / MH_ZIP_CENTRAL_BYTES)
		return invalid(error_out);

	zip->entries =
		calloc(count == 0 ? 1 : (size_t)count, sizeof(*zip->entries));

	if (zip->entries == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory indexing ZIP");

	zip->count = (size_t)count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
valid_name(char *name, size_t size, int *directory)
{
	size_t i, start, length;

	if (size == 0 || size >= MH_ZIP_PATH_MAX)
		return 0;

	*directory = name[size - 1] == '/';

	if (*directory)
		name[--size] = 0;

	start = 0;

	for (i = 0; i <= size; i++) {
		if (i < size &&
			(name[i] == 0 || name[i] == '\\' || name[i] == ':' ||
				(unsigned char)name[i] < 32))
			return 0;

		if (i < size && name[i] != '/')
			continue;

		length = i - start;

		if (length == 0 || length > 255 ||
			(length == 1 && name[start] == '.') ||
			(length == 2 && name[start] == '.' &&
				name[start + 1] == '.'))
			return 0;

		start = i + 1;
	}

	return 1;
}

static enum mh_error_code
read_local(struct mh_zip_resources *zip, struct mh_zip_entry *entry,
	const unsigned char *central, size_t name_size,
	struct mh_error *error_out)
{
	unsigned char header[MH_ZIP_LOCAL_BYTES];
	char name[MH_ZIP_PATH_MAX];
	unsigned long offset, extra;
	enum mh_error_code status;

	offset = mh_read_u32le(central + MH_ZIP_CENTRAL_LOCAL_OFFSET);
	entry->local = offset;

	if (offset > zip->central || zip->central - offset < sizeof(header))
		return invalid(error_out);

	status = read_at(zip, offset, header, sizeof(header), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (mh_read_u32le(header) != MH_ZIP_LOCAL_SIGNATURE ||
		memcmp(header + MH_ZIP_LOCAL_FLAGS,
			central + MH_ZIP_CENTRAL_FLAGS,
			MH_ZIP_LOCAL_TIME - MH_ZIP_LOCAL_FLAGS) != 0 ||
		memcmp(header + MH_ZIP_LOCAL_CRC, central + MH_ZIP_CENTRAL_CRC,
			MH_ZIP_LOCAL_NAME_SIZE - MH_ZIP_LOCAL_CRC) != 0 ||
		mh_read_u16le(header + MH_ZIP_LOCAL_NAME_SIZE) != name_size)
		return invalid(error_out);

	offset += sizeof(header);
	extra = mh_read_u16le(header + MH_ZIP_LOCAL_EXTRA_SIZE);

	if (name_size > zip->central - offset ||
		extra > zip->central - offset - name_size)
		return invalid(error_out);

	status = read_at(zip, offset, name, name_size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (memcmp(name, entry->path, name_size - entry->directory) != 0 ||
		(entry->directory && name[name_size - 1] != '/'))
		return invalid(error_out);

	entry->offset = offset + name_size + extra;

	if (entry->packed > zip->central - entry->offset)
		return invalid(error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_entry(struct mh_zip_resources *zip, struct mh_zip_entry *entry,
	unsigned long *cursor, unsigned long end, struct mh_error *error_out)
{
	unsigned char header[MH_ZIP_CENTRAL_BYTES];
	unsigned long name_size, extra, comment, flags, mode;
	enum mh_error_code status;

	if (*cursor > end || end - *cursor < sizeof(header))
		return invalid(error_out);

	status = read_at(zip, *cursor, header, sizeof(header), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	flags = mh_read_u16le(header + MH_ZIP_CENTRAL_FLAGS);

	entry->method = mh_read_u16le(header + MH_ZIP_CENTRAL_METHOD);

	/* Allow UTF-8 names and DEFLATE's two compression-level hint bits. */
	if ((entry->method != MH_ZIP_STORED &&
		entry->method != MH_ZIP_DEFLATED) ||
		(flags &
			~(entry->method == MH_ZIP_DEFLATED
					? (unsigned long)(MH_ZIP_FLAG_UTF8 |
						MH_ZIP_DEFLATE_HINTS)
					: (unsigned long)MH_ZIP_FLAG_UTF8)) !=
			0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"package requires stored or deflated, unencrypted ZIP entries without data descriptors");

	name_size = mh_read_u16le(header + MH_ZIP_CENTRAL_NAME_SIZE);
	extra = mh_read_u16le(header + MH_ZIP_CENTRAL_EXTRA_SIZE);
	comment = mh_read_u16le(header + MH_ZIP_CENTRAL_COMMENT_SIZE);
	entry->size = mh_read_u32le(header + MH_ZIP_CENTRAL_SIZE);
	entry->packed = mh_read_u32le(header + MH_ZIP_CENTRAL_PACKED_SIZE);
	entry->crc = mh_read_u32le(header + MH_ZIP_CENTRAL_CRC);
	mode = mh_read_u32le(header + MH_ZIP_CENTRAL_EXTERNAL_ATTRIBUTES) >> 16;

	if (mh_read_u32le(header) != MH_ZIP_CENTRAL_SIGNATURE ||
		mh_read_u16le(header + MH_ZIP_CENTRAL_DISK) != 0 ||
		name_size == 0 || name_size >= MH_ZIP_PATH_MAX ||
		(entry->method == MH_ZIP_STORED &&
			entry->size != entry->packed) ||
		entry->packed == MH_U32_MAX || entry->size == MH_U32_MAX ||
		((mode & ZIP_UNIX_TYPE_MASK) != 0 &&
			(mode & ZIP_UNIX_TYPE_MASK) != ZIP_UNIX_FILE &&
			(mode & ZIP_UNIX_TYPE_MASK) != ZIP_UNIX_DIRECTORY))
		return invalid(error_out);

	*cursor += sizeof(header);

	if (name_size + extra + comment > end - *cursor)
		return invalid(error_out);

	entry->path = malloc((size_t)name_size + 1);

	if (entry->path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading ZIP name");

	status = read_at(
		zip, *cursor, entry->path, (size_t)name_size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	entry->path[name_size] = 0;

	if (!valid_name(entry->path, (size_t)name_size, &entry->directory) ||
		(entry->directory && (entry->size != 0 || entry->crc != 0)))
		return invalid(error_out);

	*cursor += name_size + extra + comment;

	return read_local(zip, entry, header, (size_t)name_size, error_out);
}

static int
compare_offsets(const void *left, const void *right)
{
	const struct mh_zip_entry *const *a, *const *b;

	a = left;
	b = right;

	return (*a)->local < (*b)->local ? -1 : (*a)->local != (*b)->local;
}

static enum mh_error_code
validate_ranges(struct mh_zip_resources *zip, struct mh_error *error_out)
{
	const struct mh_zip_entry **entries;
	size_t i;
	enum mh_error_code status;

	if (zip->count < 2)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	entries = malloc(zip->count * sizeof(*entries));

	if (entries == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory validating ZIP ranges");

	for (i = 0; i < zip->count; i++)
		entries[i] = &zip->entries[i];

	qsort(entries, zip->count, sizeof(*entries), compare_offsets);
	status = MH_ERROR_NONE;

	for (i = 1; i < zip->count; i++) {
		if (entries[i - 1]->offset + entries[i - 1]->packed >
			entries[i]->local) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"overlapping ZIP entries");
			break;
		}
	}

	free(entries);

	return status;
}

static enum mh_error_code
validate_tree(struct mh_zip_resources *zip, struct mh_error *error_out)
{
	size_t i, length;
	char path[MH_ZIP_PATH_MAX], *slash;
	struct mh_zip_entry key, *parent;

	qsort(zip->entries, zip->count, sizeof(*zip->entries),
		mh_zip_compare_entries);

	for (i = 0; i < zip->count; i++) {
		if (i != 0 &&
			mh_ascii_equal(
				zip->entries[i - 1].path, zip->entries[i].path))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"ambiguous ZIP entry names");

		strcpy(path, zip->entries[i].path);
		slash = path;

		while ((slash = strchr(slash, '/')) != NULL) {
			*slash = 0;
			key.path = path;
			parent = bsearch(&key, zip->entries, zip->count,
				sizeof(*zip->entries), mh_zip_compare_entries);
			length = (size_t)(slash - path);

			if (i != 0 &&
				mh_ascii_equal_n(zip->entries[i - 1].path, path,
					length) &&
				(zip->entries[i - 1].path[length] == '/' ||
					zip->entries[i - 1].path[length] ==
						0) &&
				strncmp(zip->entries[i - 1].path, path,
					length) != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"ambiguous ZIP directory names");

			if (parent != NULL &&
				(!parent->directory ||
					strncmp(parent->path,
						zip->entries[i].path,
						length) != 0))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"conflicting ZIP directory");

			*slash++ = '/';
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_zip_index(struct mh_zip_resources *zip, struct mh_error *error_out)
{
	unsigned long size, cursor, end;
	size_t i;
	enum mh_error_code status;

	size = 0;
	status = mh_resource_source_size(
		zip->source, zip->path, &zip->length, error_out);

	if (status == MH_ERROR_NONE)
		status = read_end(zip, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	cursor = zip->central;
	end = cursor + size;

	for (i = 0; i < zip->count; i++) {
		status = read_entry(
			zip, &zip->entries[i], &cursor, end, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (cursor != end)
		return invalid(error_out);

	status = validate_ranges(zip, error_out);

	if (status == MH_ERROR_NONE)
		status = validate_tree(zip, error_out);

	return status;
}
