#ifndef MH_NET_PROTOCOL_H
#define MH_NET_PROTOCOL_H

#include <stddef.h>

#include "engine/core/error.h"
#include "engine/core/sha256.h"

#define MH_NET_CHAT_BYTES 1024
/* Chat header plus the text terminator. */
#define MH_NET_CHAT_OVERHEAD 10
#define MH_NET_PACKET_LIMIT 1024
/* Original queued send requires used + message_size + 4 < 1024. */
#define MH_NET_BATCH_LIMIT 1019
#define MH_NET_TEXT_LIMIT 1018
#define MH_NET_PLAYERS 8
/* Patched wire capacity is independent of the playable session limit. */
#define MH_NET_WIRE_PLAYERS 13
#define MH_NET_STATISTICS_ROWS (MH_NET_WIRE_PLAYERS - 1)
/* 8192-byte transport receive buffer, less the one-byte delivery prefix. */
#define MH_NET_WIRE_PACKET_LIMIT 8191
/* Maximum text length: packet limit less the rejection header and NUL. */
#define MH_NET_WIRE_TEXT_LIMIT 8185

#define MH_NET_NAME_BYTES 16
#define MH_NET_TEAM_BYTES 5
#define MH_NET_PASSWORD_BYTES 16
#define MH_NET_VEHICLE_BYTES 30
#define MH_NET_VEHICLE_FAR_BYTES 7
#define MH_NET_ORIGINAL_VERSION_BITS 0x400ae148UL
/*
 * Native fixed-tick protocol 4.0, explicitly incompatible with original peers.
 */
#define MH_NET_FIXED_VERSION_BITS 0x40800000UL

enum mh_net_wire {
	MH_NET_WIRE_F12,
	MH_NET_WIRE_31_LOBBY
};

enum mh_net_port {
	/* Original 3.1 address parsing. */
	MH_NET_LANINET_PORT = 16000
};

enum mh_net_provider_id {
	MH_NET_PROVIDER_IP = 2,
	MH_NET_PROVIDER_LANINET = 4
};

/* Profile and type-15 status bits, distinct from vehicle snapshot flags. */
enum mh_net_player_flag {
	MH_NET_FLAG_RACING = 1,
	MH_NET_FLAG_LOBBY = 2,
	MH_NET_FLAG_RECORDING = 4,
	MH_NET_FLAG_UNAVAILABLE = 8,
	MH_NET_FLAG_PRESENT = MH_NET_FLAG_RACING | MH_NET_FLAG_LOBBY,
	MH_NET_PROFILE_FLAGS = MH_NET_FLAG_PRESENT | MH_NET_FLAG_RECORDING,
	MH_NET_STATUS_FLAGS = MH_NET_PROFILE_FLAGS | MH_NET_FLAG_UNAVAILABLE
};

/* Dedicated 3.1 roster/profile flags; ordinary listen games do not set them. */
enum mh_net_dedicated_flag {
	MH_NET_FLAG_READY = 16,
	MH_NET_FLAG_PASS = 32
};

enum mh_net_mode {
	MH_NET_MODE_NORMAL = 0,
	MH_NET_MODE_ELIMINATION = 1
};

/* Profile values in wire order. */
enum mh_net_profile_value {
	MH_NET_PROFILE_COLOR = 0,
	MH_NET_PROFILE_CAR_COLOR1 = 1,
	MH_NET_PROFILE_CAR_COLOR2 = 2,
	MH_NET_PROFILE_CAR_COLOR3 = 3,
	MH_NET_PROFILE_RACES = 4,
	MH_NET_PROFILE_POINTS = 5,
	MH_NET_PROFILE_VALUES = 6
};

enum mh_net_type {
	MH_NET_JOIN = 1,
	MH_NET_ACCEPT = 2,
	MH_NET_REJECT = 3,
	MH_NET_PROFILE_JOIN = 4,
	MH_NET_PROFILE_UPDATE = 5,
	MH_NET_ROSTER = 6,
	MH_NET_CHAT = 7,
	MH_NET_NOTICE = 8,
	MH_NET_RACE_CONFIG = 9,
	MH_NET_PING = 10,
	MH_NET_LOBBY_PONG = 11,
	MH_NET_PONG = 12,
	MH_NET_CAR = 13,
	MH_NET_CAR_FAR = 14,
	MH_NET_STATUS = 15,
	MH_NET_START = 16,
	MH_NET_LEAVE_LOBBY = 17,
	MH_NET_LEAVE_RACE = 18,
	MH_NET_KICK = 19,
	MH_NET_LAST_RACE = 21,
	/* OpenMH only: reliable notification that race resources are ready. */
	MH_NET_LOADED = 22
};

struct mh_net_profile {
	unsigned int	flags, update_rate, gear_mode;
	char		name[MH_NET_NAME_BYTES], team[MH_NET_TEAM_BYTES];
	char		car[MH_NET_NAME_BYTES], horn[MH_NET_NAME_BYTES];
	unsigned long	values[MH_NET_PROFILE_VALUES];
	unsigned int	value_a, value_b;

	/* engine/input/control_type.h; retains all original byte values. */
	unsigned int	control_type;
};

enum {
	MH_NET_NONCE_BYTES = 16
};

struct mh_net_challenge {
	int		active;
	unsigned char	nonce[MH_NET_NONCE_BYTES];
};

struct mh_net_join {
	/* The receiving transport supplies the challenge locally. */
	struct mh_net_challenge	challenge;
	int			has_proof;
	unsigned char		proof[MH_SHA256_BYTES];
	unsigned int		provider;

	/* IEEE binary32; preserves original bits */
	unsigned long		version_bits;
	char			password[MH_NET_PASSWORD_BYTES];
};

struct mh_net_roster {
	char			track[MH_NET_NAME_BYTES];
	unsigned int		mode, laps, player_count, car_count;

	/*
	 * Only player_count entries are interpreted; remaining wire bytes may
	 * contain stale data and are retained for lossless forwarding.
	 */
	struct mh_net_profile	players[MH_NET_WIRE_PLAYERS];
};

struct mh_net_status {
	/* mh_net_player_flag; unavailable means stale or eliminated. */
	unsigned int	flags;
	unsigned long	ping_ms;

	/* race position and lap */
	unsigned int	value_a, value_b;
};

/* Dedicated type 21: rows are ordered by result, not by player slot. */
struct mh_net_statistics_row {
	/* Fixed-width bytes; unused rows and name padding may be stale. */
	char		name[MH_NET_NAME_BYTES];
	unsigned long	color;
	unsigned int	position, top_speed_kmh;
	unsigned long	best_lap_ms, race_ms;
};

struct mh_net_statistics {
	struct mh_net_statistics_row	rows[MH_NET_STATISTICS_ROWS];
};

struct mh_net_message {
	/*
	 * Native ACCEPT extension; both zero denotes the legacy five-byte
	 * reply.
	 */
	unsigned int			timing_version, simulation_rate;
	unsigned int			type, player, view_player;
	unsigned long			stamp, color;
	char				text[MH_NET_WIRE_TEXT_LIMIT + 1];

	/* Detailed body, or MH_NET_VEHICLE_FAR_BYTES for a distant car. */
	unsigned char			car[MH_NET_VEHICLE_BYTES];
	struct mh_net_status		status[MH_NET_WIRE_PLAYERS];
	struct mh_net_join		join;
	unsigned int			reason;
	struct mh_net_profile		profile;
	struct mh_net_roster		roster;
	struct mh_net_statistics	statistics;
};

struct mh_net_frame {
	const unsigned char	*data;
	size_t			size;
	unsigned int		type;
};

struct mh_net_reader {
	const unsigned char	*data;
	size_t			size, offset;
};

struct mh_net_batch {
	unsigned char	data[MH_NET_PACKET_LIMIT];
	size_t		size;
};

/*
 * Decoded vehicle snapshot with effect channels in wire order. This interface
 * does not apply a remote pose.
 */
struct mh_net_vehicle {
	double		extension[4], steering, wheel_speed[2], rpm, slide;
	unsigned int	gear;
	double		auxiliary[4];   /* Effect gains in wire order. */
	unsigned int	codes[4]; /* Effect codes in wire order. */

	/* Spark corners: local 0..6; wire/relay 0..3. */
	unsigned int	variants[2];
	unsigned int	flags;
	double		angles[3], position[3];
};

/* Native selection name: laninet. Failure preserves output. */
int	mh_net_wire_parse(const char *name, enum mh_net_wire *output);

/*
 * These are application messages, not DirectPlay transport packets. All
 * error-returning operations preserve outputs on failure. Begin validates the
 * entire batch, including every supported payload. Unknown nonzero types are
 * framed but cannot be decoded. Borrowed packet bytes must remain unchanged
 * throughout iteration. No callbacks, allocation, OS clock, socket, or
 * simulation mutation occurs here.
 */
int			mh_net_supported(unsigned int type);
enum mh_error_code	mh_net_begin(const unsigned char *data, size_t size,
	struct mh_net_reader *output, struct mh_error *error_out);

/* 1 = frame, 0 = end, -1 = invalid reader. */
int			mh_net_next(struct mh_net_reader *reader,
	struct mh_net_frame *output);
enum mh_error_code	mh_net_decode(const struct mh_net_frame *frame,
	struct mh_net_message *output, struct mh_error *error_out);
enum mh_error_code	mh_net_encode(const struct mh_net_message *message,
	unsigned char *output, size_t capacity, size_t *written,
	struct mh_error *error_out);
void			mh_net_batch_reset(struct mh_net_batch *batch);
enum mh_error_code	mh_net_batch_append(struct mh_net_batch *batch,
	const struct mh_net_message *message, struct mh_error *error_out);

/*
 * Explicit wire selection. Legacy entry points above retain f1.2 sizes.
 * Patched messages include setup, readiness, race state and last-race results.
 * Session owners enforce their own transition and role rules.
 * Wire rosters have 13 profiles, but owners must enforce their session limits.
 * Begin validates an entire aggregate before exposing any frame. Unknown
 * nonzero types can be iterated but decode reports unsupported. The same wire
 * selection must be used for begin, next and decode; no auto-detection occurs.
 * Batches keep the bounded native chunk size; every append must use one wire.
 */
enum mh_error_code	mh_net_wire_batch_append(enum mh_net_wire wire,
	struct mh_net_batch *batch, const struct mh_net_message *message,
	struct mh_error *error_out);
int			mh_net_wire_supported(enum mh_net_wire wire,
	unsigned int type);
enum mh_error_code	mh_net_wire_begin(enum mh_net_wire wire,
	const unsigned char *data, size_t size, struct mh_net_reader *output,
	struct mh_error *error_out);
int			mh_net_wire_next(enum mh_net_wire wire,
	struct mh_net_reader *reader, struct mh_net_frame *output);
enum mh_error_code	mh_net_wire_decode(enum mh_net_wire wire,
	const struct mh_net_frame *frame, struct mh_net_message *output,
	struct mh_error *error_out);
enum mh_error_code	mh_net_wire_encode(enum mh_net_wire wire,
	const struct mh_net_message *message, unsigned char *output,
	size_t capacity, size_t *written, struct mh_error *error_out);

enum mh_error_code	mh_net_vehicle_decode(const unsigned char *data,
	size_t size, struct mh_net_vehicle *output, struct mh_error *error_out);

/*
 * Original quantizers. Input channels are rounded to binary32 before packing.
 * Angles must already be normalized radians in [0, 2*pi rounded to float];
 * matrix conversion is separate. Finite channel bounds prevent undefined
 * float-to-integer conversions. Size is exactly 7 or 30; output is unchanged on
 * error. Far positions must fit signed 16-bit /16 coordinates, so callers can
 * select full state instead of reproducing the original overflow wrap. Full
 * position writes require an IEEE binary32 native float (normal
 * little/big-endian layouts).
 */
enum mh_error_code	mh_net_vehicle_encode(
	const struct mh_net_vehicle *vehicle, unsigned char *output,
	size_t size, struct mh_error *error_out);

/*
 * Original host re-quantization of decoded remote state. Retains its
 * nonzero-flags quirk: any set flag makes output bits 8..10 all set. Allows
 * decoded angles through 65535/8192 and 12 flag bits. Raw lossless forwarding
 * uses mh_net_encode, without this step.
 */
enum mh_error_code	mh_net_vehicle_relay_encode(
	const struct mh_net_vehicle *vehicle, unsigned char *output,
	size_t size, struct mh_error *error_out);

#endif
