#ifndef MH_MENU_DIALOG_H
#define MH_MENU_DIALOG_H

#include <stddef.h>

#include "engine/core/error.h"
#include "engine/ui/menu.h"

#define MH_MENU_DIALOG_TEXT_BYTES 256

struct mh_menu_dialog_text {
	/* NUL-separated lists, each terminated by an empty string. */
	unsigned char	title[MH_MENU_DIALOG_TEXT_BYTES];
	unsigned char	choices[MH_MENU_DIALOG_TEXT_BYTES];
	unsigned int	title_count, choice_count;
};

/*
 * Copy lists into two 256-byte buffers. Preserve byte glyph codes; reject
 * unterminated/oversized lists atomically. Empty title is permitted, but
 * choices must contain at least one nonempty string.
 */
enum mh_error_code	mh_menu_dialog_text_read(const unsigned char *title,
	size_t title_size, const unsigned char *choices, size_t choices_size,
	struct mh_menu_dialog_text *output, struct mh_error *error_out);

/* Original English titles and choice labels for the built-in dialogs. */
enum mh_error_code	mh_menu_dialog_builtin(unsigned int kind,
	struct mh_menu_dialog_text *output, struct mh_error *error_out);

#endif
