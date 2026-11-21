#ifndef MH_MESSAGES_H
#define MH_MESSAGES_H

#include "engine/core/error.h"
#include "interfaces/font/glyphs.h"
#define MH_MESSAGE_ROWS 100
#define MH_MESSAGE_BYTES 256
#define MH_MESSAGE_VISIBLE_ROWS 5

struct mh_message {
	char		text[MH_MESSAGE_BYTES];
	unsigned long	color, milliseconds;
};

struct mh_messages {
	struct mh_message	rows[MH_MESSAGE_ROWS]; /* Newest first. */
	unsigned long		width, revision;
};

struct mh_message_line {
	unsigned int	row;
	int		x, y;
};

struct mh_message_view {
	struct mh_message_line	lines[MH_MESSAGE_VISIBLE_ROWS];
	unsigned int		count;

	/* Original repeated request during newest timestamp +500ms. */
	int			sound;
};

void	mh_messages_init(struct mh_messages *messages);

/* Clear text and timestamps, retain colors, width and revision. */
void	mh_messages_clear(struct mh_messages *messages);

/* Zero retains the previous width. Accepts an unsigned disk word. */
enum mh_error_code	mh_messages_width(struct mh_messages *messages,
	unsigned long width, struct mh_error *error_out);

/*
 * Concatenate, truncate to 255 bytes, wrap by per-byte advance. The overflowing
 * byte starts the next line but its width is not re-counted. All lines share
 * the supplied clock sample. Text may alias an existing row. Input errors leave
 * the entire history unchanged.
 */
enum mh_error_code	mh_messages_add(struct mh_messages *messages,
	const char *text, const char *suffix, unsigned long color,
	unsigned long milliseconds, const unsigned int advance[MH_FONT_GLYPHS],
	struct mh_error *error_out);

/*
 * Newest five entries, oldest first in fixed slots. Expiry adds 5000/500 in
 * unsigned 32-bit arithmetic before comparing with game time. Empty unexpired
 * entries still have a slot. Read-only, atomic on error.
 */
enum mh_error_code	mh_messages_view(const struct mh_messages *messages,
	double milliseconds, unsigned int height, unsigned int font_height,
	struct mh_message_view *view, struct mh_error *error_out);

#endif
