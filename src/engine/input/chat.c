#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"
#include "engine/input/chat.h"
#include "engine/input/text_edit.h"

static int
history_valid(const struct mh_chat_history *history)
{
	unsigned int i;

	if (history == NULL || history->recall >= MH_CHAT_HISTORY_ROWS)
		return 0;

	for (i = 0; i < MH_CHAT_HISTORY_ROWS; i++)
		if (memchr(history->rows[i].text, 0, MH_CHAT_TEXT_BYTES) ==
				NULL ||
			history->rows[i].words[0] > MH_U32_MAX ||
			history->rows[i].words[1] > MH_U32_MAX)
			return 0;

	return 1;
}

static void
copy_draft(
	struct mh_chat_draft *destination, const struct mh_chat_draft *source)
{
	/* The original copies through the first NUL, not the whole disk row. */
	strcpy((char *)destination->text, (const char *)source->text);

	destination->words[0] = source->words[0];
	destination->words[1] = source->words[1];
}

static void
update_history(
	struct mh_chat_history *history, enum mh_chat_history_action action)
{
	unsigned int i;

	if (action == MH_CHAT_STORE) {
		for (i = MH_CHAT_HISTORY_ROWS - 1; i != 0; i--)
			copy_draft(&history->rows[i], &history->rows[i - 1]);

		history->rows[0].text[0] = 0;
	} else if (action == MH_CHAT_NEWER) {
		if (history->recall != 0) {
			history->recall--;

			if (history->recall == 0)
				history->rows[0].text[0] = 0;
			else
				copy_draft(&history->rows[0],
					&history->rows[history->recall]);
		}
	} else if (action == MH_CHAT_OLDER) {
		if (history->recall < MH_CHAT_HISTORY_ROWS - 1)
			history->recall++;

		copy_draft(&history->rows[0], &history->rows[history->recall]);
	} else
		history->recall = 0;
}

enum mh_error_code
mh_chat_history_update(struct mh_chat_history *history,
	enum mh_chat_history_action action, struct mh_error *error_out)
{
	if (!history_valid(history) || action < MH_CHAT_STORE ||
		action > MH_CHAT_RESET_RECALL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid chat history");

	update_history(history, action);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
append_message(struct mh_chat_output *out, const struct mh_chat_config *config,
	const unsigned char *text)
{
	struct mh_chat_send *line;

	line = &out->sends[out->count++];
	strcpy(line->text, config->name);
	strcat(line->text, ": ");
	strcat(line->text, (const char *)text);
	line->color = config->color;
	line->network = config->mode == MH_GAME_NETWORK;
}

enum mh_error_code
mh_chat_step(struct mh_chat *chat, const struct mh_chat_config *config,
	const struct mh_control_binding_set *bindings,
	const struct mh_binding_input *input,
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[256], struct mh_chat_output *output,
	struct mh_error *error_out)
{
	struct mh_chat next;
	struct mh_chat_output out;
	struct mh_text_edit_output edited;
	enum mh_error_code status;
	unsigned int i, active;
	unsigned long key_code;

	if (chat == NULL || config == NULL || bindings == NULL ||
		input == NULL || characters == NULL || advance == NULL ||
		output == NULL || !history_valid(&chat->history) ||
		chat->active > MH_U32_MAX || chat->cancelled > MH_U32_MAX ||
		config->mode > MH_U32_MAX || config->color > MH_U32_MAX ||
		memchr(config->name, 0, sizeof(config->name)) == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid race chat");

	for (i = 0; i < MH_CHAT_SHORTCUTS; i++)
		if (memchr(config->shortcuts[i], 0,
			sizeof(config->shortcuts[i])) == NULL ||
			bindings->codes[MH_CONTROL_DIGIT_0 + i] >
				MH_BINDING_SCAN_MASK)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid chat shortcut");

	next = *chat;
	memset(&out, 0, sizeof(out));
	out.block_controls = out.skip_menu = next.active != 0;

	if (next.active) {
		status = mh_text_edit(next.history.rows[0].text,
			MH_CHAT_TEXT_BYTES, MH_CHAT_EDIT_LIMIT,
			MH_CHAT_EDIT_WIDTH, input->events, characters, advance,
			&edited, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (input->events[MH_KEY_ENTER]) {
			append_message(&out, config, next.history.rows[0].text);
			update_history(&next.history, MH_CHAT_STORE);
			update_history(&next.history, MH_CHAT_RESET_RECALL);
			next.active = 0;
		}

		if (input->events[MH_KEY_ESCAPE]) {
			update_history(&next.history, MH_CHAT_STORE);
			next.active = 0;
			next.cancelled = 1;
		}
	} else if (config->mode == MH_GAME_NETWORK) {
		status = mh_binding_query(input,
			bindings->codes[MH_CONTROL_CHAT], 1, &active, NULL,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (active)
			next.active = 1;

		for (i = 0; i < MH_CHAT_SHORTCUTS; i++) {
			key_code = bindings->codes[MH_CONTROL_DIGIT_0 + i];

			if (input->events[key_code])
				append_message(&out, config,
					(const unsigned char *)
						config->shortcuts[i]);
		}
	}

	*chat = next;
	*output = out;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
