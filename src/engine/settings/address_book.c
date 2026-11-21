#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/settings/address_book.h"
#include "engine/settings/parse.h"
#include "engine/storage/path_names.h"

#define NPC_FIRST_ORDINAL 2

struct profile_reader {
	struct mh_address_profile	profile;
	int				described;
};

static void
read_description(
	char name[MH_ADDRESS_NAME_BYTES], char **tokens, unsigned int count)
{
	unsigned int i;
	size_t length, token_length;

	length = 0;

	for (i = 1; i < count && length < MH_ADDRESS_NAME_BYTES - 1; i++) {
		if (i != 1)
			name[length++] = ' ';

		token_length = strlen(tokens[i]);

		if (token_length > MH_ADDRESS_NAME_BYTES - 1 - length)
			token_length = MH_ADDRESS_NAME_BYTES - 1 - length;

		memcpy(name + length, tokens[i], token_length);
		length += token_length;
	}

	name[length] = 0;
}

static enum mh_error_code
read_profile_line(char **tokens, unsigned int count, void *context,
	struct mh_error *error_out)
{
	struct profile_reader *reader;

	reader = (struct profile_reader *)context;

	if (count < 2)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (mh_ascii_equal(tokens[0], "Description")) {
		reader->described = 1;
		read_description(reader->profile.name, tokens, count);
	}

	if (mh_ascii_equal(tokens[0], "Type")) {
		if (mh_ascii_equal(tokens[1], "TCP"))
			reader->profile.tcp = 1;
		else if (mh_ascii_equal(tokens[1], "IPX") ||
			mh_ascii_equal(tokens[1], "DialUp") ||
			mh_ascii_equal(tokens[1], "Serial"))
			reader->profile.tcp = 0;
	}

	if (mh_ascii_equal(tokens[0], "Address") && reader->profile.tcp) {
		if (strlen(tokens[1]) >= sizeof(reader->profile.ip))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"NPC address exceeds 127 bytes");

		strcpy(reader->profile.ip, tokens[1]);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_address_profile_read(const unsigned char *text, size_t size,
	unsigned int ordinal, struct mh_address_profile *output,
	struct mh_error *error_out)
{
	struct profile_reader reader;
	enum mh_error_code status;

	if (output == NULL || ordinal < NPC_FIRST_ORDINAL ||
		ordinal > MH_ADDRESS_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid NPC output or ordinal");

	memset(&reader, 0, sizeof(reader));
	status =
		mh_cfg_parse(text, size, read_profile_line, &reader, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!reader.described && reader.profile.tcp)
		sprintf(reader.profile.name, "TCP/IP_%02u", ordinal);

	*output = reader.profile;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_address_profile_encode(const struct mh_address_profile *profile, char *text,
	size_t capacity, size_t *size, struct mh_error *error_out)
{
	char next[MH_ADDRESS_TEXT_CAPACITY];
	size_t length;

	if (profile == NULL || profile->tcp != 1 ||
		!mh_text_field_valid(profile->name, sizeof(profile->name)) ||
		!mh_text_field_valid(profile->ip, sizeof(profile->ip)) ||
		size == NULL || (text == NULL && capacity != 0))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid TCP NPC output");

	sprintf(next,
		"// Motorhead TCP/IP address\nDescription\t%s\nType\t\tTCP\nAddress\t\t%s\n",
		profile->name, profile->ip);
	length = strlen(next);

	if (text != NULL) {
		if (capacity <= length)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"NPC output buffer too small");

		memcpy(text, next, length + 1);
	}

	*size = length;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_address_filename(const char *name, const char *const *existing,
	unsigned int count, char output[MH_ADDRESS_FILENAME_BYTES],
	struct mh_error *error_out)
{
	char next[MH_ADDRESS_FILENAME_BYTES];
	unsigned int i, byte;
	size_t length, offset;

	if (name == NULL || output == NULL || count > MH_ADDRESS_LIMIT ||
		(count != 0 && existing == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid NPC filename input");

	for (i = 0; i < count; i++) {
		if (existing[i] == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing NPC filename");

		for (offset = 0; offset < MH_ADDRESS_FILENAME_BYTES &&
			existing[i][offset] != 0;
			offset++) {
		}

		if (offset == MH_ADDRESS_FILENAME_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated NPC filename");
	}

	length = 0;

	for (offset = 0; offset < MH_ADDRESS_NAME_BYTES && name[offset] != 0;
		offset++) {
		byte = (unsigned char)name[offset];

		if (byte >= 32 && byte <= 127 &&
			strchr(":;<>?*\\./", (int)byte) == NULL)
			next[length++] = (char)byte;
	}

	if (offset == MH_ADDRESS_NAME_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "NPC display name too long");

	if (length == 0)
		next[length++] = '!';

	for (;;) {
		memcpy(next + length, MH_USER_ADDRESS_EXTENSION,
			sizeof(MH_USER_ADDRESS_EXTENSION));
		mh_ascii_lower(next);

		for (i = 0; i < count; i++)
			if (mh_ascii_equal(next, existing[i]))
				break;

		if (i == count)
			break;

		if (length == sizeof(next) - sizeof(MH_USER_ADDRESS_EXTENSION))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"NPC filename collision suffix exceeds capacity");

		next[length++] = '!';
	}

	memcpy(output, next, length + sizeof(MH_USER_ADDRESS_EXTENSION));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_address_book_free(struct mh_address_book *book)
{
	if (book == NULL)
		return;

	free(book->entries);
	memset(book, 0, sizeof(*book));
}

static enum mh_error_code
validate_sources(const struct mh_address_source *sources, unsigned int count,
	struct mh_error *error_out)
{
	const char *filename;
	unsigned int i, j;
	size_t length;

	for (i = 0; i < count; i++) {
		filename = sources[i].filename;

		if (!mh_text_field_valid(filename, MH_ADDRESS_FILENAME_BYTES) ||
			(sources[i].text == NULL && sources[i].size != 0))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid NPC source");

		length = strlen(filename);

		if (length < sizeof(MH_USER_ADDRESS_EXTENSION) ||
			!mh_ascii_equal(filename + length -
					(sizeof(MH_USER_ADDRESS_EXTENSION) - 1),
				MH_USER_ADDRESS_EXTENSION) ||
			strpbrk(filename, "/\\:") != NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid NPC basename");

		for (j = 0; j < i; j++)
			if (mh_ascii_equal(filename, sources[j].filename))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"ambiguous NPC filenames");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_address_book_read(const struct mh_address_source *sources,
	unsigned int count, struct mh_address_book *output,
	struct mh_error *error_out)
{
	struct mh_address_book next;
	struct mh_address_profile profile;
	enum mh_error_code status;
	unsigned int i;

	if (output == NULL || output->entries != NULL || output->count != 0 ||
		count >= MH_ADDRESS_LIMIT || (count != 0 && sources == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid address book input");

	status = validate_sources(sources, count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));
	next.entries = (struct mh_address_entry *)calloc(
		count + 1, sizeof(*next.entries));

	if (next.entries == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory loading address book");

	next.count = 1;
	strcpy(next.entries[0].profile.name, "TCP/IP");
	next.entries[0].profile.tcp = 1;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < count && status == MH_ERROR_NONE; i++) {
		if (sources[i].text == NULL)
			continue;

		status = mh_address_profile_read(sources[i].text,
			sources[i].size, i + NPC_FIRST_ORDINAL, &profile,
			error_out);

		if (status != MH_ERROR_NONE || !profile.tcp)
			continue;

		next.entries[next.count].profile = profile;
		strcpy(next.entries[next.count].filename, sources[i].filename);
		next.count++;
	}

	if (status != MH_ERROR_NONE) {
		mh_address_book_free(&next);
		return status;
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_address_book_add(struct mh_address_book *book, const char *name,
	const char *ip, struct mh_error *error_out)
{
	struct mh_address_entry entry, *entries;
	const char **names;
	enum mh_error_code status;
	unsigned int i;

	if (book == NULL || book->entries == NULL || book->count == 0 ||
		book->count >= MH_ADDRESS_LIMIT ||
		!mh_text_field_valid(name, MH_ADDRESS_NAME_BYTES) ||
		!mh_text_field_valid(ip, MH_ADDRESS_IP_BYTES))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid new TCP address");

	memset(&entry, 0, sizeof(entry));
	entry.profile.tcp = 1;
	strcpy(entry.profile.name, name);
	strcpy(entry.profile.ip, ip);
	names = (const char **)malloc(book->count * sizeof(*names));

	if (names == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory naming TCP address");

	for (i = 0; i < book->count; i++)
		names[i] = book->entries[i].filename;

	status = mh_address_filename(
		name, names, book->count, entry.filename, error_out);
	free(names);

	if (status != MH_ERROR_NONE)
		return status;

	entries = (struct mh_address_entry *)realloc(
		book->entries, (book->count + 1) * sizeof(*entries));

	if (entries == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory adding TCP address");

	entries[book->count] = entry;
	book->entries = entries;
	book->count++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_address_book_sort(struct mh_address_book *book, unsigned int *previous)
{
	struct mh_address_entry entry;
	unsigned int i, j, previous_index;

	if (previous != NULL)
		for (i = 0; i < book->count; i++)
			previous[i] = i;

	for (i = 2; i < book->count; i++) {
		entry = book->entries[i];
		previous_index = previous == NULL ? 0 : previous[i];
		j = i;

		while (j > 1 &&
			memcmp(book->entries[j - 1].profile.name,
				entry.profile.name,
				MH_ADDRESS_NAME_BYTES) > 0) {
			book->entries[j] = book->entries[j - 1];

			if (previous != NULL)
				previous[j] = previous[j - 1];

			j--;
		}

		book->entries[j] = entry;

		if (previous != NULL)
			previous[j] = previous_index;
	}
}

enum mh_error_code
mh_address_book_remove(struct mh_address_book *book, unsigned int index,
	struct mh_error *error_out)
{
	if (book == NULL || book->entries == NULL || book->count == 0 ||
		book->count > MH_ADDRESS_LIMIT || index == 0 ||
		index >= book->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or protected TCP address removal");

	memmove(book->entries + index, book->entries + index + 1,
		(book->count - index - 1) * sizeof(*book->entries));
	book->count--;
	memset(book->entries + book->count, 0, sizeof(*book->entries));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
