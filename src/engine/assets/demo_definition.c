#include <stdio.h>
#include <string.h>

#include "engine/assets/demo_definition.h"
#include "engine/core/binary.h"

#define RECORD_STRING_BYTES 256

static enum mh_error_code
append_text(struct mh_definition_text *text, const char *value,
	struct mh_error *error_out)
{
	size_t length;

	length = strlen(value);

	if (length >= sizeof(text->data) - text->size)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"binary definition exceeds text limit");

	memcpy(text->data + text->size, value, length + 1);
	text->size += length;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_string(struct mh_definition_text *text, const unsigned char *bytes,
	int directory, struct mh_error *error_out)
{
	char value[RECORD_STRING_BYTES];
	size_t length;

	for (length = 0; length < sizeof(value) && bytes[length] != 0; length++)
		if (bytes[length] < 32 || bytes[length] > 126)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid binary definition string");

	if (length == 0 || length == sizeof(value))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"empty or unterminated binary definition string");

	while (directory && length != 0 &&
		(bytes[length - 1] == '/' || bytes[length - 1] == '\\'))
		length--;

	if (length == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"empty binary definition directory");

	memcpy(value, bytes, length);
	value[length] = 0;

	return append_text(text, value, error_out);
}

static enum mh_error_code
append_numbers(struct mh_definition_text *text, const unsigned char *bytes,
	const struct mh_definition_field *field, struct mh_error *error_out)
{
	char value[64];
	double number;
	unsigned long word;
	unsigned int i;
	enum mh_error_code status;

	for (i = 0; i < field->count; i++) {
		if (field->kind == MH_DEFINITION_BYTES_RGB)
			sprintf(value, " %u", (unsigned int)bytes[i]);
		else if (field->kind == MH_DEFINITION_FLOAT) {
			if (!mh_read_f32le(bytes + i * 4, &number))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"non-finite binary definition number");

			/*
			 * Preserve the exact binary32 value after promotion to
			 * double.
			 */
			sprintf(value, " %.17g", number);
		} else {
			word = mh_read_u32le(bytes + i * 4);

			if (field->kind == MH_DEFINITION_COLOR)
				sprintf(value, " %lu %lu %lu",
					(word >> 16) & 255, (word >> 8) & 255,
					word & 255);
			else
				sprintf(value, " %lu", word);
		}

		status = append_text(text, value, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_definition_write(struct mh_definition_text *text,
	const unsigned char *record, const struct mh_definition_field *field,
	struct mh_error *error_out)
{
	const unsigned char *bytes;
	enum mh_error_code status;

	bytes = record + field->offset;
	status = append_text(text, field->name, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (field->kind == MH_DEFINITION_STRING ||
		field->kind == MH_DEFINITION_DIRECTORY ||
		field->kind == MH_DEFINITION_PAIR) {
		status = append_text(text, " ", error_out);

		if (status == MH_ERROR_NONE)
			status = append_string(text, bytes,
				field->kind == MH_DEFINITION_DIRECTORY,
				error_out);

		if (status == MH_ERROR_NONE &&
			field->kind == MH_DEFINITION_PAIR) {
			status = append_text(text, " ", error_out);

			if (status == MH_ERROR_NONE)
				status = append_string(text,
					bytes + RECORD_STRING_BYTES, 0,
					error_out);
		}
	} else
		status = append_numbers(text, bytes, field, error_out);

	return status == MH_ERROR_NONE ? append_text(text, "\n", error_out)
				: status;
}

enum mh_error_code
mh_definition_paths(struct mh_definition_text *text, const unsigned char *names,
	size_t size, const char *key, struct mh_error *error_out)
{
	struct mh_definition_field field;
	const unsigned char *end;
	size_t offset, length;
	enum mh_error_code status;

	field.name = key;
	field.offset = 0;
	field.kind = MH_DEFINITION_STRING;
	field.count = 1;
	offset = 0;

	while (offset < size && names[offset] != 0) {
		end = memchr(names + offset, 0, size - offset);

		if (end == NULL)
			break;

		length = (size_t)(end - (names + offset));

		if (length >= RECORD_STRING_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"binary definition list name is too long");

		status = mh_definition_write(
			text, names + offset, &field, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		offset += length + 1;
	}

	if (offset >= size || names[offset] != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated binary definition path list");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
