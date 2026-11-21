#ifndef MH_NET_LOBBY_H
#define MH_NET_LOBBY_H

#include "engine/core/error.h"
#include "engine/network/protocol.h"

enum mh_net_lobby_phase {
	MH_NET_EMPTY,
	MH_NET_RESERVED,
	MH_NET_IN_LOBBY,
	MH_NET_IN_RACE
};

struct mh_net_lobby_slot {
	enum mh_net_lobby_phase	phase;

	/* Adapter-owned identity, not a payload player index. */
	unsigned long		peer;
	double			reserved_ms;
	struct mh_net_profile	profile;
};

struct mh_net_lobby {
	unsigned int			simulation_rate;
	struct mh_net_lobby_slot	slots[MH_NET_PLAYERS];
	unsigned int			provider;
	char				password[MH_NET_PASSWORD_BYTES];
	double				now_ms;
};

/*
 * Original admission/profile phase only. Slot zero is the local host.
 * Adapters bind remote connections to nonzero 32-bit peer IDs >=2; 1 is
 * reserved for the server. A trusted adapter must supply sender identity.
 * Native clocks are monotonic milliseconds, 0..1e12, independent of wire
 * timestamps. No sockets, UI, asset lookup or race transition occurs here.
 * Callers must resolve car/horn names against their asset catalog before
 * using them. Error returns preserve host state and outputs.
 */
enum mh_error_code	mh_net_lobby_init(unsigned int provider,
	const char *password, const struct mh_net_profile *host,
	struct mh_net_lobby *output, struct mh_error *error_out);
enum mh_error_code	mh_net_lobby_admit(struct mh_net_lobby *lobby,
	unsigned long peer, const struct mh_net_message *request, double now_ms,
	struct mh_net_message *reply, struct mh_error *error_out);
enum mh_error_code	mh_net_lobby_profile(struct mh_net_lobby *lobby,
	unsigned long peer, const struct mh_net_message *message, double now_ms,
	struct mh_error *error_out);

/*
 * Host is running a race while this peer joins/edits in the lobby. Admission
 * uses the same reset, but edits retain the old horn.
 */
enum mh_error_code	mh_net_lobby_race_profile(struct mh_net_lobby *lobby,
	unsigned long peer, const struct mh_net_message *message, double now_ms,
	struct mh_error *error_out);
enum mh_error_code	mh_net_lobby_expire(struct mh_net_lobby *lobby,
	double now_ms, struct mh_error *error_out);

/* Transport disconnection, not an unverified wire player index. */
enum mh_error_code	mh_net_lobby_disconnect(struct mh_net_lobby *lobby,
	unsigned long peer, struct mh_error *error_out);

#endif
