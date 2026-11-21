#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/material.h"
#include "engine/assets/tbl.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define SOUND_FLAGS_MAX 15
#define SOUND_VOLUME_MAX 100
#define SOUND_PITCH_MIN .01
#define SOUND_PITCH_MAX 16
#define SOUND_LOOP_SAMPLE_LIMIT 32000000

enum {
	LINE_BYTES = 1024,
	SURFACE_LIMIT = 26,
	EFFECT_COUNT = 4
};

enum sound_field {
	SOUND_FLAGS,
	SOUND_PRIORITY,
	SOUND_VOLUME,
	SOUND_PITCH,
	SOUND_LOOP_START,
	SOUND_LOOP_TAIL,
	SOUND_FIELD_COUNT
};

struct scalar_setting {
	const char	*key;
	size_t		offset;
	double		fallback;
	double		maximum;
};

#define FIELD(key, field, value, maximum)                                      \
	{key, offsetof(struct mh_material, field), value, maximum}
static const struct scalar_setting scalar_settings[] = {
	FIELD("spinspeedlimit", spin_speed, 7, 1e6),
	FIELD("spinrotlimit", spin_rot, 26000, 1e9),
	FIELD("xspinfriction", x_spin, .2, 1),
	FIELD("xnospinfriction", x_roll, .000555, 1),
	FIELD("zspinfriction", z_spin, .796714, 1),
	FIELD("znospinfriction", z_roll, .858086, 1),
	FIELD("rotspinfriction", rot_spin, .010429, 1),
	FIELD("rotnospinfriction", rot_roll, .000007, 1),
	FIELD("spinturnreduce", turn_spin, .9, 1),
	FIELD("nospinturnreduce", turn_roll, 1, 1),
	FIELD("bumpy", bumpy, 0, 100),
	FIELD("spinwheellimit", wheel_spin, 2000, 1e6),
	FIELD("nospinwheellimit", wheel_roll, 3000, 1e6)};
#undef FIELD

enum {
	SCALAR_COUNT = sizeof(scalar_settings) / sizeof(scalar_settings[0]),
	SOUND_SEEN_SHIFT = SCALAR_COUNT,
	EFFECT_SEEN_SHIFT = SCALAR_COUNT + MH_MATERIAL_SOUNDS
};

static void
lower_ascii(char *text)
{
	while (*text != 0) {
		if (*text >= 'A' && *text <= 'Z')
			*text += 'a' - 'A';

		text++;
	}
}

static char *
next_token(char **cursor)
{
	char *start;

	while (**cursor == ' ' || **cursor == '\t')
		(*cursor)++;

	start = *cursor;

	while (**cursor != 0 && **cursor != ' ' && **cursor != '\t')
		(*cursor)++;

	if (**cursor != 0)
		*(*cursor)++ = 0;

	return start;
}

static enum mh_error_code
parse_sound(char **cursor, struct mh_material_sound *sound,
	struct mh_error *error_out)
{
	char *value, *end;
	double values[SOUND_FIELD_COUNT];
	volatile float volume, pitch;
	unsigned int i;
	static const double limits[SOUND_FIELD_COUNT] = {SOUND_FLAGS_MAX,
		(double)MH_U32_MAX, SOUND_VOLUME_MAX, SOUND_PITCH_MAX,
		SOUND_LOOP_SAMPLE_LIMIT, SOUND_LOOP_SAMPLE_LIMIT};

	value = next_token(cursor);

	if (*value == 0 || strlen(value) >= sizeof(sound->sample) ||
		strchr(value, '/') != NULL || strchr(value, '\\') != NULL ||
		strchr(value, ':') != NULL || strcmp(value, ".") == 0 ||
		strcmp(value, "..") == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid material sample basename");

	lower_ascii(value);
	strcpy(sound->sample, value);

	for (i = 0; i < SOUND_FIELD_COUNT; i++) {
		value = next_token(cursor);
		errno = 0;
		values[i] = strtod(value, &end);

		if (errno != 0 || end == value || *end != 0 ||
			!(values[i] >= 0 && values[i] <= limits[i]) ||
			(i == SOUND_PITCH && values[i] < SOUND_PITCH_MIN) ||
			((i < SOUND_VOLUME || i > SOUND_PITCH) &&
				values[i] != (unsigned long)values[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid material sound parameter");
	}

	if (*next_token(cursor) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"extra material sound parameter");

	/*
	 * C89 permits excess precision across casts; force the disk float
	 * stores.
	 */
	volume = (float)values[SOUND_VOLUME];
	pitch = (float)values[SOUND_PITCH];
	sound->flags = (unsigned long)values[SOUND_FLAGS];
	sound->priority = (unsigned long)values[SOUND_PRIORITY];
	sound->volume = volume;
	sound->pitch = pitch;
	sound->loop_start = (unsigned long)values[SOUND_LOOP_START];
	sound->loop_tail = (unsigned long)values[SOUND_LOOP_TAIL];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
material_defaults(struct mh_material *material)
{
	unsigned int i;

	memset(material, 0, sizeof(*material));

	for (i = 0; i < SCALAR_COUNT; i++) {
		/* Store defaults directly as binary32. */
		*(double *)((char *)material + scalar_settings[i].offset) =
			(double)(float)scalar_settings[i].fallback;
	}
}

static int
find_material(const struct mh_materials *table, const char *name)
{
	unsigned int i;

	for (i = 0; i < table->count; i++) {
		if (strcmp(table->entries[i].name, name) == 0)
			return (int)i;
	}

	return -1;
}

static enum mh_error_code
parse_property(const char *key, char **cursor, struct mh_material *material,
	unsigned long *seen_fields, struct mh_error *error_out)
{
	char *value, *end;
	double number;
	unsigned int i;
	enum mh_error_code status;
	static const char *sound_keys[MH_MATERIAL_SOUNDS] = {"sndcontact",
		"sndweakimpulse", "sndhardimpulse", "sndscratch", "sndslide",
		"sndspin"};

	static const char *effect_keys[EFFECT_COUNT] = {
		"slidetype", "sparkanimid", "contactanimid", "skidmark"};

	static const size_t effect_offsets[EFFECT_COUNT] = {
		offsetof(struct mh_material, slide_type),
		offsetof(struct mh_material, spark_anim),
		offsetof(struct mh_material, contact_anim),
		offsetof(struct mh_material, skid_mark)};

	for (i = 0; i < SCALAR_COUNT; i++) {
		if (strcmp(key, scalar_settings[i].key) != 0)
			continue;

		value = next_token(cursor);
		errno = 0;
		number = strtod(value, &end);

		if (material == NULL || (*seen_fields & (1UL << i)) != 0 ||
			errno != 0 || end == value || *end != 0 ||
			*next_token(cursor) != 0 ||
			!(number >= 0 && number <= scalar_settings[i].maximum))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid or duplicate material setting");

		*(double *)((char *)material + scalar_settings[i].offset) =
			number;
		*seen_fields |= 1UL << i;
		break;
	}

	for (i = 0; i < MH_MATERIAL_SOUNDS; i++) {
		if (strcmp(key, sound_keys[i]) != 0)
			continue;

		if (material == NULL ||
			(*seen_fields & (1UL << (SOUND_SEEN_SHIFT + i))) != 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid or duplicate material sound");

		status = parse_sound(cursor, &material->sounds[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		*seen_fields |= 1UL << (SOUND_SEEN_SHIFT + i);
		break;
	}

	for (i = 0; i < EFFECT_COUNT; i++) {
		if (strcmp(key, effect_keys[i]) != 0)
			continue;

		value = next_token(cursor);
		errno = 0;
		number = strtod(value, &end);

		if (material == NULL ||
			(*seen_fields & (1UL << (EFFECT_SEEN_SHIFT + i))) !=
				0 ||
			errno != 0 || end == value || *end != 0 ||
			*next_token(cursor) != 0 ||
			!(number >= 0 && number <= (double)MH_U32_MAX) ||
			number != (unsigned long)number)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid or duplicate material effect setting");

		*(unsigned long *)((char *)material + effect_offsets[i]) =
			(unsigned long)number;
		*seen_fields |= 1UL << (EFFECT_SEEN_SHIFT + i);
		break;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
parse_list(const unsigned char *text, size_t size, int surface_names,
	struct mh_materials *output, struct mh_error *error_out)
{
	char line[LINE_BYTES];
	char *cursor;
	char *key;
	char *value;
	char *end;
	char *comment;
	size_t offset;
	size_t length;
	unsigned int expected_count;
	unsigned long seen_fields;
	double number;
	enum mh_error_code status;

	memset(output, 0, sizeof(*output));
	expected_count = seen_fields = 0;
	offset = 0;

	while (offset < size) {
		length = 0;

		while (offset < size && text[offset] != '\r' &&
			text[offset] != '\n') {
			if ((text[offset] < 32 && text[offset] != '\t') ||
				text[offset] > 126)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"non-ASCII material text");

			if (length == sizeof(line) - 1)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"material line too long");

			line[length++] = (char)text[offset++];
		}

		if (offset < size && text[offset] == '\r')
			offset++;

		if (offset < size && text[offset] == '\n')
			offset++;

		line[length] = 0;
		comment = strstr(line, "//");

		if (comment != NULL)
			*comment = 0;

		cursor = line;
		key = next_token(&cursor);
		lower_ascii(key);

		if (strcmp(key, "materials") == 0) {
			value = next_token(&cursor);
			errno = 0;
			number = strtod(value, &end);

			if (expected_count != 0 || errno != 0 || end == value ||
				*end != 0 || *next_token(&cursor) != 0 ||
				!(number >= 1 &&
					number <=
						(surface_names ? SURFACE_LIMIT
							: MH_MATERIAL_LIMIT)) ||
				number != (unsigned int)number)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate material count");

			expected_count = (unsigned int)number;
		} else if (strcmp(key,
				surface_names ? "material"
						: "materialname") == 0) {
			value = next_token(&cursor);
			lower_ascii(value);

			if (expected_count == 0 ||
				output->count == expected_count ||
				*value == 0 ||
				strlen(value) >=
					sizeof(output->entries[0].name) ||
				*next_token(&cursor) != 0 ||
				find_material(output, value) >= 0) {
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate material name");
			}

			material_defaults(&output->entries[output->count]);
			strcpy(output->entries[output->count++].name, value);
			seen_fields = 0;
		} else if (!surface_names) {
			status = parse_property(key, &cursor,
				output->count == 0
					? NULL
					: &output->entries[output->count - 1],
				&seen_fields, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	return expected_count == 0 || output->count != expected_count
		? mh_error_set(error_out, MH_ERROR_INVALID,
			"material count mismatch")
		: mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
parse_surfaces(const unsigned char *data, size_t size,
	struct mh_materials *output, struct mh_error *error_out)
{
	unsigned char *text;
	size_t text_size;
	enum mh_error_code status;

	/* Patch 1.4's reverse NeoCity surface list uses the TBL wrapper. */
	if (size < 3 || memcmp(data, "TBL", 3) != 0)
		return parse_list(data, size, 1, output, error_out);

	status = mh_tbl_decode(data, size, &text, &text_size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = parse_list(text, text_size, 1, output, error_out);
	free(text);

	return status;
}

enum mh_error_code
mh_materials_mat_srf_decode(const unsigned char *definitions, size_t size,
	const unsigned char *surfaces, size_t surface_size,
	struct mh_materials *output, struct mh_error *error_out)
{
	struct mh_materials parsed_definitions;
	struct mh_materials result;
	enum mh_error_code status;
	unsigned int i;
	int slot;

	if (definitions == NULL || surfaces == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid material arguments");

	status = parse_list(
		definitions, size, 0, &parsed_definitions, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = parse_surfaces(surfaces, surface_size, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	result.surface_count = result.count;

	for (i = 0; i < parsed_definitions.count; i++) {
		slot = find_material(
			&result, parsed_definitions.entries[i].name);

		if (slot < 0) {
			if (result.count == MH_MATERIAL_LIMIT)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"combined material table too large");

			slot = (int)result.count++;
		}

		result.entries[slot] = parsed_definitions.entries[i];
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_materials_mat_srf_decode_list(const unsigned char *text, size_t size,
	int surface_names, struct mh_materials *output,
	struct mh_error *error_out)
{
	struct mh_materials parsed;
	enum mh_error_code status;

	if (text == NULL || output == NULL ||
		(surface_names != 0 && surface_names != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid material list arguments");

	status = parse_list(text, size, surface_names, &parsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	parsed.surface_count = surface_names ? parsed.count : 0;
	*output = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
