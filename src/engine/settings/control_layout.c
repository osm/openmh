#include <float.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/input/keyboard.h"
#include "engine/settings/control_layout_internal.h"
#include "engine/settings/parse.h"

const char *const mh_control_action_names[MH_CONTROL_FILE_BINDINGS] = {
	"TurnLeft", "TurnRight", "Accelerate", "Brake", "ShiftGearUp",
	"ShiftGearDown", "Horn", "HandBrake", "RearView", "GameMenu", "Info",
	"ChatMode", "InCarView", "OutCarView", "CameraView", "RearMirror",
	"CyclePlayers", "ScreenShot"};

const char *const mh_control_mouse_keys[MH_CONTROL_MOUSE_AXES] = {
	"MouseSpeedX", "MouseSpeedY", "MouseSpeedZ"};

struct layout_reader {
	struct mh_control_layout	layout;
	const char			*const *key_names;
};

enum mh_error_code
mh_control_layout_validate(
	const struct mh_control_layout *layout, struct mh_error *error_out)
{
	unsigned int i;

	if (layout == NULL ||
		!mh_text_field_valid(layout->name, sizeof(layout->name)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid control layout name");

	if (layout->force_feedback > 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid control force feedback preference");

	for (i = 0; i < MH_CONTROL_MOUSE_AXES; i++)
		if (!(layout->mouse_speed[i] >= -FLT_MAX &&
			layout->mouse_speed[i] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid control mouse speed");

	for (i = 0; i < MH_CONTROL_BINDINGS; i++)
		if (layout->bindings[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid control binding");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/*
 * 256 records of 20 bytes. Retain duplicate Undefined labels: lookup selects
 * the first matching index.
 */
static const char *const fallback_names[MH_KEY_COUNT] = {"Undefined00",
	"Escape", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "Minus",
	"Equals", "Back", "Tab", "Q", "W", "E", "R", "T", "Y", "U", "I", "O",
	"P", "LeftBracket", "RightBracket", "Return", "LeftControl", "A", "S",
	"D", "F", "G", "H", "J", "K", "L", "SemiColon", "Apostrophe",
	"AccentGrave", "LeftShift", "BackSlash", "Z", "X", "C", "V", "B", "N",
	"M", "Comma", "Period", "Slash", "RightShift", "NumMultiply", "LeftAlt",
	"Space", "CapsLock", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8",
	"F9", "F10", "NumLock", "ScrollLock", "Num7", "Num8", "Num9",
	"NumSubtract", "Num4", "Num5", "Num6", "NumAdd", "Num1", "Num2", "Num3",
	"Num0", "NumDecimal", "Undefined54", "Undefined55", "Greater", "F11",
	"F12", "Undefined59", "Undefined5A", "Undefined5B", "Undefined5C",
	"Undefined5D", "Undefined5E", "Undefined5F", "Undefined60",
	"Undefined61", "Undefined62", "Undefined63", "F13", "F14", "F15",
	"Undefined67", "Undefined68", "Undefined69", "Undefined6A",
	"Undefined6B", "Undefined6C", "Undefined6D", "Undefined6E",
	"Undefined6F", "Kana", "Undefined71", "Undefined72", "Undefined73",
	"Undefined74", "Undefined75", "Undefined76", "Undefined77",
	"Undefined78", "Convert", "Undefined7A", "NoConvert", "Undefined7C",
	"Yen", "Undefined7E", "Undefined7F", "Undefined80", "Undefined81",
	"Undefined82", "Undefined83", "Undefined84", "Undefined85",
	"Undefined86", "Undefined87", "Undefined88", "Undefined89",
	"Undefined8A", "Undefined8B", "Undefined8C", "NumEquals", "Undefined8E",
	"Undefined8F", "Circumflex", "At", "Colon", "Underline", "Kanji",
	"Stop", "Ax", "Unlabeled", "Undefined98", "Undefined99", "Undefined9A",
	"Undefined9B", "NumEnter", "RightControl", "Undefined9E", "Undefined9F",
	"UndefinedA0", "UndefinedA0", "UndefinedA1", "UndefinedA2",
	"UndefinedA3", "UndefinedA4", "UndefinedA5", "UndefinedA6",
	"UndefinedA7", "UndefinedA8", "UndefinedA9", "UndefinedAA",
	"UndefinedAB", "UndefinedAC", "UndefinedAD", "UndefinedAE",
	"UndefinedAF", "UndefinedB0", "UndefinedB1", "UndefinedB2", "NumComma",
	"NumDivide", "UndefinedB6", "SysRequest", "RightAlt", "UndefinedB9",
	"UndefinedBA", "UndefinedBB", "UndefinedBC", "UndefinedBD",
	"UndefinedBE", "UndefinedBF", "UndefinedC0", "UndefinedC1",
	"UndefinedC2", "UndefinedC3", "UndefinedC4", "UndefinedC5",
	"UndefinedC6", "Home", "UpArrow", "PageUp", "UndefinedCA", "LeftArrow",
	"UndefinedCC", "RightArrow", "UndefinedDE", "End", "DownArrow",
	"PageDown", "Insert", "Delete", "UndefinedD4", "UndefinedD5",
	"UndefinedD6", "UndefinedD7", "UndefinedD8", "UndefinedD9",
	"UndefinedDA", "LeftWindows", "RightWindows", "AppMenu", "UndefinedDE",
	"UndefinedDF", "UndefinedE0", "UndefinedE1", "UndefinedE2",
	"UndefinedE3", "UndefinedE4", "UndefinedE5", "UndefinedE6",
	"UndefinedE7", "UndefinedE8", "MouseIn", "MouseOut", "JoyButton8",
	"JoyButton7", "JoyButton6", "JoyButton5", "JoyButton4", "JoyButton3",
	"JoyButton2", "JoyButton1", "JoyLeft", "JoyRight", "JoyUp", "JoyDown",
	"JoyIn", "JoyOut", "MouseLeft", "MouseRight", "MouseUp", "MouseDown",
	"MouseButtonLeft", "MouseButtonRight", "MouseButtonMiddle"};

const char *
mh_control_key_name(unsigned int code)
{
	if (code >= MH_KEY_COUNT)
		return NULL;

	if (code == MH_KEY_ESCAPE)
		return "Esc";

	if (code == MH_KEY_LCTRL)
		return "Ctrl";

	if (code == MH_KEY_LSHIFT)
		return "Shift";

	if (code == MH_KEY_LALT)
		return "Alt";

	return fallback_names[code];
}

static unsigned long
find_key_code(const char *name, const char *const *key_names)
{
	unsigned int i;
	const char *label;

	for (i = 0; i < MH_KEY_COUNT; i++) {
		label = key_names == NULL ? mh_control_key_name(i)
					: key_names[i];

		if (label != NULL && mh_ascii_equal(name, label))
			return i;
	}

	/* Accept the names saved by older native builds at these scan codes. */
	if (key_names == NULL) {
		if (mh_ascii_equal(name, "UndefinedE9"))
			return MH_KEY_MOUSE_IN;

		if (mh_ascii_equal(name, "UndefinedEA"))
			return MH_KEY_MOUSE_OUT;
	}

	return MH_CONTROL_UNBOUND;
}

static enum mh_error_code
read_mouse_speed(const char *text, float *output, struct mh_error *error_out)
{
	double value, precision, limit;
	unsigned int digit;

	if (!mh_cfg_number(text, &value))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"control mouse speed out of range");

	if (!(value >= -FLT_MAX && value <= FLT_MAX)) {
		/*
		 * Decimal settings can fall just beyond the float range and
		 * still round to its endpoint. Clamp inside the half-ULP
		 * interval before casting. The midpoint rounds to infinity.
		 */
		precision = 1;

		for (digit = 0; digit < FLT_MANT_DIG; digit++)
			precision *= FLT_RADIX;

		limit = (double)FLT_MAX +
			(double)FLT_MAX / (2 * (precision - 1));

		if (!(value > -limit && value < limit))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"control mouse speed out of range");

		value = value < 0 ? -(double)FLT_MAX : (double)FLT_MAX;
	}

	*output = (float)value;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
layout_line(char **tokens, unsigned int count, void *context,
	struct mh_error *error_out)
{
	struct layout_reader *reader;
	unsigned int i;
	unsigned long code;
	enum mh_error_code status;

	reader = (struct layout_reader *)context;

	if (count != 2)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (mh_ascii_equal(tokens[0], "LayoutName")) {
		if (strlen(tokens[1]) >= sizeof(reader->layout.name))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"control layout name too long");

		strcpy(reader->layout.name, tokens[1]);
	}

	for (i = 0; i < MH_CONTROL_MOUSE_AXES; i++)
		if (mh_ascii_equal(tokens[0], mh_control_mouse_keys[i]) ||
			(i == 0 && mh_ascii_equal(tokens[0], "MouseSpeedH")) ||
			(i == 1 && mh_ascii_equal(tokens[0], "MouseSpeedV"))) {
			status = read_mouse_speed(tokens[1],
				&reader->layout.mouse_speed[i], error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

	if (mh_ascii_equal(tokens[0], "ForceFeedback")) {
		if (mh_ascii_equal(tokens[1], "On"))
			reader->layout.force_feedback = 1;
		else if (mh_ascii_equal(tokens[1], "Off"))
			reader->layout.force_feedback = 0;
	}

	for (i = 0; i < MH_CONTROL_FILE_BINDINGS; i++)
		if (mh_ascii_equal(tokens[0], mh_control_action_names[i])) {
			code = find_key_code(tokens[1], reader->key_names);

			if (code < MH_KEY_COUNT)
				reader->layout.bindings[i] = code;
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_control_layout_read_at(const unsigned char *text, size_t size,
	const char *const *key_names, unsigned int ordinal,
	struct mh_control_layout *output, struct mh_error *error_out)
{
	static const char *const defaults[MH_CONTROL_BINDINGS] = {"LeftArrow",
		"RightArrow", "UpArrow", "DownArrow", "A", "S", "Shift",
		"Space", "Ctrl", "Esc", "Tab", "T", "F1", "F2", "F3", "F5",
		"F4", "F12", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

	struct layout_reader reader;
	enum mh_error_code status;
	unsigned int i;

	if (output == NULL || ordinal == 0 || ordinal > MH_CONTROL_PROFILES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid control layout output or ordinal");

	memset(&reader, 0, sizeof(reader));
	reader.key_names = key_names;
	sprintf(reader.layout.name, "Layout%02u", ordinal);

	for (i = 0; i < MH_CONTROL_MOUSE_AXES; i++)
		reader.layout.mouse_speed[i] = 1;

	for (i = 0; i < MH_CONTROL_BINDINGS; i++)
		reader.layout.bindings[i] =
			find_key_code(defaults[i], key_names);

	status = mh_cfg_parse(text, size, layout_line, &reader, error_out);

	if (status == MH_ERROR_NONE)
		*output = reader.layout;

	return status;
}

enum mh_error_code
mh_control_layout_read(const unsigned char *text, size_t size,
	const char *const *key_names, struct mh_control_layout *output,
	struct mh_error *error_out)
{
	return mh_control_layout_read_at(
		text, size, key_names, 1, output, error_out);
}
