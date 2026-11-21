#ifndef MH_UI_LOBBY_SCREEN_H
#define MH_UI_LOBBY_SCREEN_H

#include "engine/core/error.h"
#include "engine/input/lobby_console.h"
#include "engine/ui/lobby_console.h"
#include "engine/ui/lobby_session.h"
#include "interfaces/image/pixels.h"

struct mh_indexed_image;
struct mh_image_decoder_provider;
struct mh_resource_source;

struct mh_lobby_screen {
	struct mh_messages		messages;
	struct mh_lobby_scroll		scroll;
	struct mh_lobby_keys		keys;
	struct mh_lobby_command_state	console;
	unsigned long			chat_revision, quit;
	int				reconnect, statistics;
};

struct mh_lobby_backdrop {
	unsigned char	*pixels;
	unsigned char	palette[MH_IMAGE_PALETTE_BYTES];
};

/*
 * 640x400 indexed Server/Client/LastRace artwork, with reserved font
 * colors. Input is decoded, top-down and borrowed for this call.
 * Output owns its pixels and remains unchanged on failure.
 */
enum mh_error_code	mh_lobby_backdrop_prepare(
	const struct mh_indexed_image *image, struct mh_lobby_backdrop *output,
	struct mh_error *error_out);

/*
 * Decode through the selected provider, then apply the palette/layout policy.
 */
enum mh_error_code	mh_lobby_backdrop_load_from(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_lobby_backdrop *output,
	struct mh_error *error_out);
void			mh_lobby_backdrop_free(
	struct mh_lobby_backdrop *backdrop);

/*
 * Update palette entries 243+slot for active players or the local slot,
 * retaining inactive entries and the backdrop/font palette.
 */
enum mh_error_code	mh_lobby_screen_palette(
	const struct mh_lobby_roster *roster,
	unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	struct mh_error *error_out);
void			mh_lobby_screen_init(struct mh_lobby_screen *screen);

/* Screen reentry resets transient timers and confirmation; scroll persists. */
void			mh_lobby_screen_enter(struct mh_lobby_screen *screen);
enum mh_error_code	mh_lobby_screen_message(struct mh_lobby_screen *screen,
	const char *text, unsigned long color, unsigned long message_ms,
	const unsigned int advance[MH_FONT_GLYPHS], struct mh_error *error_out);

/* Observe after every network event, before its bounded chat window rolls. */
enum mh_error_code	mh_lobby_screen_collect(struct mh_lobby_screen *screen,
	const struct mh_net_session *session, unsigned long message_ms,
	const unsigned int advance[MH_FONT_GLYPHS], struct mh_error *error_out);

/*
 * Console editing/submission, scroll, clipboard/F keys in original order.
 * Session, screen and editable history commit together. Clock callbacks are
 * external observations. Poll network next, then call the Escape boundary.
 * Caller consumes reconnect and demo_path, and refreshes palette before draw.
 * transport_ms belongs to the session; message_ms is the truncated sampled
 * game clock. Hotkey delays use input->clock_ms independently of both.
 */
enum mh_error_code	mh_lobby_screen_input(struct mh_lobby_screen *screen,
	struct mh_net_session *session, struct mh_chat_history *history,
	const struct mh_lobby_command_catalog *commands,
	const struct mh_lobby_key_catalog *keys,
	const struct mh_lobby_key_input *input,
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[MH_FONT_GLYPHS], double transport_ms,
	unsigned long message_ms, struct mh_error *error_out);

/*
 * Rejected client's password editor followed by the same paste/F6/F7 block.
 * Paste targets the chat draft; password mode skips chat submission, recall
 * and scrolling. Enter sets reconnect without skipping simultaneous hotkeys.
 * The connection owner consumes reconnect with the retained local profile.
 * All three states commit together, including the edited password.
 */
enum mh_error_code	mh_lobby_screen_password_input(
	struct mh_lobby_screen *screen, struct mh_net_session *session,
	struct mh_chat_history *history,
	const struct mh_lobby_key_catalog *keys,
	const struct mh_lobby_key_input *input,
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[MH_FONT_GLYPHS], double transport_ms,
	unsigned long message_ms, struct mh_error *error_out);

#endif
