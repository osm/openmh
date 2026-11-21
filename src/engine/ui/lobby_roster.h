#ifndef MH_UI_LOBBY_ROSTER_H
#define MH_UI_LOBBY_ROSTER_H

#include "engine/core/error.h"
#include "engine/network/protocol.h"
#include "engine/settings/av.h"
#include "engine/ui/menu_text.h"
#include "interfaces/image/pixels.h"

#define MH_LOBBY_ROSTER_PLAYERS 13
/* Nine settings lines, plus both roster passes with recording per slot. */
#define MH_LOBBY_ROSTER_LINES (9 + 21 * MH_LOBBY_ROSTER_PLAYERS)

enum mh_lobby_roster_layout {
	MH_LOBBY_ROSTER_F12,
	MH_LOBBY_ROSTER_31
};

struct mh_lobby_roster_player {
	char		name[17], team[5], car[17];
	unsigned long	racing, lobby, recording, gear, color;
	unsigned long	races, points, position, lap, ping;
	unsigned long	ready, pass;
};

struct mh_lobby_roster {
	enum mh_lobby_roster_layout	layout;

	/* Optional borrowed patched last-race table, in server result order. */
	const struct mh_net_statistics	*statistics;
	unsigned int			system;
	unsigned int			player, count;
	unsigned long			password_mode, mode, laps;
	char				session[17], password[17], track[17];
	struct mh_lobby_roster_player	players[MH_LOBBY_ROSTER_PLAYERS];
};

struct mh_lobby_roster_line {
	unsigned int	x, y, color;
	unsigned char	text[32];
};

struct mh_lobby_roster_view {
	unsigned int			count;

	/* Derived team totals. */
	unsigned long			team_points[MH_LOBBY_ROSTER_PLAYERS];
	struct mh_lobby_roster_line	lines[MH_LOBBY_ROSTER_LINES];
};

/*
 * Settings, active racers in rank order, then lobby players in stable
 * descending unsigned score order. Flags are independent: a slot can occur in
 * both original passes. FONT1 advances determine name/car truncation; palette
 * is 256 RGB triples. Password mode omits settings but still draws both roster
 * sections. Derived team totals are returned, not written into game/player
 * state. The patched layout uses normal twelve-pixel rows, compacting to fit
 * a full roster above chat, and ready/pass markers instead of the older
 * recording marker. Ready takes precedence over pass. Atomic
 * output on error; no input mutation, rendering or network I/O.
 */
enum mh_error_code	mh_lobby_roster_view(
	const struct mh_lobby_roster *roster,
	const unsigned int advance[MH_FONT_GLYPHS],
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	struct mh_lobby_roster_view *view, struct mh_error *error_out);

/*
 * Compose the same lines with decoded FONT1. The caller supplies the
 * original Server/Client backdrop, palette and separate console region.
 */
enum mh_error_code	mh_lobby_roster_draw(
	const struct mh_lobby_roster *roster, const struct mh_font *font,
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
