#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_dialog.h"

static enum mh_error_code
read_list(const unsigned char *data, size_t size,
	unsigned char output[MH_MENU_DIALOG_TEXT_BYTES], unsigned int *count,
	struct mh_error *error_out)
{
	size_t position, end;

	if (data == NULL || size == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing dialog text list");

	position = 0;
	*count = 0;

	while (position < size && position < MH_MENU_DIALOG_TEXT_BYTES) {
		if (data[position] == 0) {
			memcpy(output, data, position + 1);
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		end = position;

		while (end < size && end < MH_MENU_DIALOG_TEXT_BYTES &&
			data[end] != 0)
			end++;

		position = end + 1;
		(*count)++;
	}

	return mh_error_set(error_out, MH_ERROR_INVALID,
		"unterminated or oversized dialog text list");
}

enum mh_error_code
mh_menu_dialog_text_read(const unsigned char *title, size_t title_size,
	const unsigned char *choices, size_t choices_size,
	struct mh_menu_dialog_text *output, struct mh_error *error_out)
{
	struct mh_menu_dialog_text parsed;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing dialog text output");

	memset(&parsed, 0, sizeof(parsed));
	status = read_list(title, title_size, parsed.title, &parsed.title_count,
		error_out);

	if (status == MH_ERROR_NONE)
		status = read_list(choices, choices_size, parsed.choices,
			&parsed.choice_count, error_out);

	if (status == MH_ERROR_NONE && parsed.choice_count == 0)
		status = mh_error_set(
			error_out, MH_ERROR_INVALID, "empty dialog choices");

	if (status == MH_ERROR_NONE)
		*output = parsed;

	return status;
}

enum mh_error_code
mh_menu_dialog_builtin(unsigned int kind, struct mh_menu_dialog_text *output,
	struct mh_error *error_out)
{
	static const unsigned char exit_title[] = "You want to exit?\0";
	static const unsigned char exit_choices[] = "Yes, please\0 No way!\0";
	static const unsigned char blocked_title[] =
		"Can't exit.\0 \0Use ALT+F4 instead\0";
	static const unsigned char blocked_choices[] = "Ok\0";
	static const unsigned char delete_title[] =
		"Delete demo file.\0 \0Are you sure?\0";
	static const unsigned char delete_choices[] = "Yes\0NO!\0";
	static const unsigned char save_title[] =
		" \0Save the recorded race?\0";
	static const unsigned char save_choices[] = "Save to disc\0Cancel\0";
	static const unsigned char overwrite_title[] = " \0File exists?\0";
	static const unsigned char overwrite_choices[] = "Overwrite\0Cancel\0";
	static const unsigned char league_create_title[] =
		"Create this league.\0 \0Are you sure?\0";
	static const unsigned char league_create_choices[] =
		"Yes, keep it!\0No\0";
	static const unsigned char league_delete_title[] =
		"Delete this league.\0 \0Are you sure?\0";
	static const unsigned char league_delete_choices[] =
		"Yes, delete\0Keep it!\0";
	static const unsigned char address_delete_title[] =
		"Delete this entry.\0 \0Are you sure?\0";

	if (kind == MH_MENU_CONFIRM_EXIT)
		return mh_menu_dialog_text_read(exit_title, sizeof(exit_title),
			exit_choices, sizeof(exit_choices), output, error_out);

	if (kind == MH_MENU_BLOCK_EXIT)
		return mh_menu_dialog_text_read(blocked_title,
			sizeof(blocked_title), blocked_choices,
			sizeof(blocked_choices), output, error_out);

	if (kind == MH_MENU_CONFIRM_DEMO_DELETE)
		return mh_menu_dialog_text_read(delete_title,
			sizeof(delete_title), delete_choices,
			sizeof(delete_choices), output, error_out);

	if (kind == MH_MENU_CONFIRM_RECORD_SAVE)
		return mh_menu_dialog_text_read(save_title, sizeof(save_title),
			save_choices, sizeof(save_choices), output, error_out);

	if (kind == MH_MENU_CONFIRM_RECORD_OVERWRITE)
		return mh_menu_dialog_text_read(overwrite_title,
			sizeof(overwrite_title), overwrite_choices,
			sizeof(overwrite_choices), output, error_out);

	if (kind == MH_MENU_CONFIRM_LEAGUE_CREATE)
		return mh_menu_dialog_text_read(league_create_title,
			sizeof(league_create_title), league_create_choices,
			sizeof(league_create_choices), output, error_out);

	if (kind == MH_MENU_CONFIRM_LEAGUE_DELETE)
		return mh_menu_dialog_text_read(league_delete_title,
			sizeof(league_delete_title), league_delete_choices,
			sizeof(league_delete_choices), output, error_out);

	if (kind == MH_MENU_CONFIRM_ADDRESS_DELETE)
		return mh_menu_dialog_text_read(address_delete_title,
			sizeof(address_delete_title), delete_choices,
			sizeof(delete_choices), output, error_out);

	return mh_error_set(error_out, MH_ERROR_INVALID, "unknown menu dialog");
}
