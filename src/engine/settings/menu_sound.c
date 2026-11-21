#include <float.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/settings/menu_sound.h"
#include "engine/settings/parse.h"

static enum mh_error_code
append_sample(struct mh_menu_sound_config *config, const char *name,
	struct mh_error *error_out)
{
	struct mh_menu_sound_sample *sample;
	const char *cursor;

	if (config->count == MH_MENU_SOUND_SAMPLES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"too many menu sound samples");

	if (strlen(name) >= sizeof(config->samples[0].name))
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "menu sample name too long");

	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu sample name");

	for (cursor = name; *cursor != 0; cursor++)
		if (*cursor == '/' || *cursor == '\\' || *cursor == ':')
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"menu sample name must be a basename");

	sample = &config->samples[config->count++];
	strcpy(sample->name, name);
	sample->volume = 1;
	sample->pitch = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sound_line(char **tokens, unsigned int count, void *context,
	struct mh_error *error_out)
{
	struct mh_menu_sound_config *config;
	struct mh_menu_sound_sample *sample;
	enum mh_error_code status;
	double value;
	long double scaled_value;
	unsigned long event;
	unsigned int token_index;
	int pitch_change;

	config = (struct mh_menu_sound_config *)context;

	if (count < 2)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (mh_ascii_equal(tokens[0], "SampleName")) {
		status = append_sample(config, tokens[1], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (config->count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	sample = &config->samples[config->count - 1];
	pitch_change = mh_ascii_equal(tokens[0], "PitchChange");

	if (pitch_change || mh_ascii_equal(tokens[0], "VolumeChange")) {
		if (!mh_cfg_number(tokens[1], &value))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu sound change");

		/* Preserve the original extended product and binary64 scale. */
		scaled_value = ((long double)value + 100) * (double).01;
		scaled_value *= pitch_change ? sample->pitch : sample->volume;

		if (!(scaled_value >= -FLT_MAX && scaled_value <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"menu sound change out of range");

		if (pitch_change)
			sample->pitch = (float)scaled_value;
		else
			sample->volume = (float)scaled_value;
	}

	if (mh_ascii_equal(tokens[0], "SampleEvent"))
		for (token_index = 1; token_index < count; token_index++) {
			if (!mh_cfg_number(tokens[token_index], &value) ||
				!mh_cfg_integer(value, &event) ||
				event >= MH_MENU_SOUND_EVENTS)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid menu sound event");

			config->events[event] = config->count;
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_sound_read(const unsigned char *text, size_t size,
	struct mh_menu_sound_config *output, struct mh_error *error_out)
{
	struct mh_menu_sound_config parsed;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing menu sound output");

	memset(&parsed, 0, sizeof(parsed));
	status = mh_cfg_parse(text, size, sound_line, &parsed, error_out);

	if (status == MH_ERROR_NONE)
		*output = parsed;

	return status;
}

unsigned int
mh_menu_sound_event(
	const struct mh_menu_sound_config *config, unsigned int event)
{
	if (config == NULL || event == 0 || event >= MH_MENU_SOUND_EVENTS)
		return 0;

	return config->events[event];
}
