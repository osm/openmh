#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/input/binding.h"
#include "engine/settings/control_layout_internal.h"
#include "engine/settings/decimal.h"

#define MOUSE_SPEED_PLACES 6
#define KEY_COMMENT_COLUMNS 4
#define KEY_COMMENT_WIDTH 18

struct layout_writer {
	char		text[MH_CONTROL_TEXT_CAPACITY];
	size_t		size;
	struct mh_error	error;
};

static void
append_text(struct layout_writer *writer, const char *text)
{
	size_t length;

	if (writer->error.code != MH_ERROR_NONE)
		return;

	length = strlen(text);

	if (length >= sizeof(writer->text) - writer->size) {
		mh_error_set(&writer->error, MH_ERROR_RANGE,
			"control layout output too large");
		return;
	}

	memcpy(writer->text + writer->size, text, length);
	writer->size += length;
	writer->text[writer->size] = 0;
}

static void
append_bindings(struct layout_writer *writer,
	const struct mh_control_layout *layout, const char *const *key_names)
{
	/* GameMenu is written last, after the other noneditable actions. */
	static const enum mh_control_action order[MH_CONTROL_FILE_BINDINGS] = {
		MH_CONTROL_LEFT,
		MH_CONTROL_RIGHT,
		MH_CONTROL_ACCELERATE,
		MH_CONTROL_BRAKE,
		MH_CONTROL_SHIFT_UP,
		MH_CONTROL_SHIFT_DOWN,
		MH_CONTROL_HORN,
		MH_CONTROL_HANDBRAKE,
		MH_CONTROL_REAR_VIEW,
		MH_CONTROL_INFO,
		MH_CONTROL_CHAT,
		MH_CONTROL_IN_CAR,
		MH_CONTROL_OUT_CAR,
		MH_CONTROL_CAMERA,
		MH_CONTROL_MIRROR,
		MH_CONTROL_CYCLE,
		MH_CONTROL_SCREENSHOT,
		MH_CONTROL_GAME_MENU,
	};

	unsigned int i, slot;
	unsigned long code;
	const char *name;

	append_text(writer, "\n//\tInGame Car-tasks and control media\n");

	for (i = 0; i < MH_CONTROL_FILE_BINDINGS; i++) {
		slot = order[i];

		if (slot == MH_CONTROL_INFO)
			append_text(writer,
				"\n//\tMisc InGame tasks (Not changeable in menu)\n");

		code = layout->bindings[slot] & MH_BINDING_SCAN_MASK;
		name = key_names == NULL
			? mh_control_key_name((unsigned int)code)
			: key_names[code];

		append_text(writer, mh_control_action_names[slot]);

		if (slot == MH_CONTROL_BRAKE || slot == MH_CONTROL_HORN ||
			slot == MH_CONTROL_INFO)
			append_text(writer, "\t\t");
		else
			append_text(writer, "\t");
		append_text(writer, name);
		append_text(writer, "\n");
	}
}

static void
append_key_catalog(struct layout_writer *writer, const char *const *key_names)
{
	const char *name;
	unsigned int i, shown;
	size_t length;

	append_text(writer, "\n\n//\tValid control codes");
	shown = 0;

	for (i = 0; i < MH_KEY_COUNT; i++) {
		name = key_names == NULL ? mh_control_key_name(i)
					: key_names[i];

		length = strlen(name);

		/* Reject any matching character, not the prefix Undefined. */
		if ((length > 0 && name[0] == 'U') ||
			(length > 1 && name[1] == 'n') ||
			(length > 2 && name[2] == 'd') ||
			(length > 3 && name[3] == 'e'))
			continue;

		if (shown % KEY_COMMENT_COLUMNS == 0)
			append_text(writer, "\n//  ");

		append_text(writer, name);

		while (length < KEY_COMMENT_WIDTH) {
			append_text(writer, " ");
			length++;
		}

		shown++;
	}
}

enum mh_error_code
mh_control_layout_encode(const struct mh_control_layout *layout,
	const char *const *key_names, char *text, size_t capacity, size_t *size,
	struct mh_error *error_out)
{
	struct layout_writer writer;
	char number[MH_SETTINGS_DECIMAL_BYTES];
	enum mh_error_code status;
	unsigned int i;

	if (size == NULL || (text == NULL && capacity != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid control layout output");

	status = mh_control_layout_validate(layout, &writer.error);

	if (status != MH_ERROR_NONE)
		return mh_error_forward(error_out, &writer.error);

	for (i = 0; i < MH_KEY_COUNT; i++)
		if (!mh_text_field_valid(key_names == NULL
				? mh_control_key_name(i)
				: key_names[i],
			MH_CONTROL_KEY_NAME_BYTES))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid control key name");

	writer.size = 0;
	writer.text[0] = 0;
	append_text(&writer,
		"// Motorhead native control layout\n\n//\tLayout name (In Menu)\nLayoutName\t");
	append_text(&writer, layout->name);
	append_text(&writer, "\n\n//\tMouse input sensitivity etc.\n");

	for (i = 0;
		i < MH_CONTROL_MOUSE_AXES && writer.error.code == MH_ERROR_NONE;
		i++) {
		status = mh_settings_decimal(layout->mouse_speed[i],
			MOUSE_SPEED_PLACES, number, &writer.error);

		if (status != MH_ERROR_NONE)
			break;

		append_text(&writer, mh_control_mouse_keys[i]);
		append_text(&writer, "\t");
		append_text(&writer, number);
		append_text(&writer, "\n");
	}

	append_text(&writer, "\n//\tJoystick controls etc\nForceFeedback\t");
	append_text(&writer, layout->force_feedback ? "On\n" : "Off\n");

	append_bindings(&writer, layout, key_names);
	append_key_catalog(&writer, key_names);

	/*
	 * Original closes before attempting its final two newline characters.
	 */
	if (writer.error.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &writer.error);

	if (text != NULL) {
		if (capacity <= writer.size)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"control layout output buffer too small");

		memcpy(text, writer.text, writer.size + 1);
	}

	*size = writer.size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
