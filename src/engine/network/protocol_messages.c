#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/simulation_rate.h"
#include "engine/network/protocol_internal.h"

#define WIRE_BYTE_MAX 255
#define PROFILE_BYTES 83
#define STATUS_BYTES 7
#define STATISTICS_ROW_BYTES 31

/* Original application message sizes, including the four-byte header. */
enum message_size {
	JOIN_BYTES = 25,
	PLAYER_BYTES = 5,
	ACCEPT_TIMING_BYTES = 13,
	REJECT_MIN_BYTES = 6,
	PROFILE_MESSAGE_BYTES = 88,
	ROSTER_BYTES = 688,
	PATCHED_ROSTER_BYTES = 1103,
	PING_BYTES = 8,
	PONG_BYTES = 9,
	CAR_BYTES = 40,
	CAR_FAR_BYTES = 16,
	STATISTICS_BYTES = MH_NET_HEADER_BYTES +
		MH_NET_STATISTICS_ROWS * STATISTICS_ROW_BYTES
};

enum message_offset {
	PLAYER_OFFSET = 4,
	ACCEPT_TIMING_VERSION = 5,
	ACCEPT_SIMULATION_RATE = 9,
	JOIN_PROVIDER_OFFSET = 4,
	REJECT_REASON_OFFSET = 4,
	JOIN_VERSION_OFFSET = 5,
	JOIN_PASSWORD_OFFSET = 9,
	REJECT_TEXT_OFFSET = 5,
	PROFILE_OFFSET = 5,
	CHAT_COLOR_OFFSET = 5,
	CHAT_TEXT_OFFSET = 9,
	PING_STAMP_OFFSET = 4,
	PONG_STAMP_OFFSET = 5,
	CAR_VIEW_OFFSET = 5,
	CAR_STAMP_OFFSET = 6,
	CAR_BODY_OFFSET = 10,
	FAR_BODY_OFFSET = 9,
	STATUS_OFFSET = 4
};

enum profile_offset {
	PROFILE_FLAGS = 0,
	PROFILE_RATE = 1,
	PROFILE_NAME = 2,
	PROFILE_TEAM = 18,
	PROFILE_GEAR = 23,
	PROFILE_CAR = 24,
	PROFILE_HORN = 40,
	PROFILE_VALUES = 56,
	PROFILE_VALUE_A = 80,
	PROFILE_VALUE_B = 81,
	PROFILE_CONTROL = 82
};

enum roster_offset {
	ROSTER_TRACK = 4,
	ROSTER_MODE = 20,
	ROSTER_LAPS = 21,
	ROSTER_PLAYERS = 22,
	ROSTER_CARS = 23,
	ROSTER_PROFILES = 24
};

enum statistics_offset {
	STATISTICS_COLOR = 16,
	STATISTICS_POSITION = 20,
	STATISTICS_SPEED = 21,
	STATISTICS_BEST_LAP = 23,
	STATISTICS_RACE_TIME = 27
};

enum status_offset {
	STATUS_FLAGS = 0,
	STATUS_PING = 1,
	STATUS_VALUE_A = 5,
	STATUS_VALUE_B = 6
};

int
mh_net_wire_parse(const char *name, enum mh_net_wire *output)
{
	if (name == NULL || output == NULL)
		return 0;

	if (strcmp(name, "laninet") == 0)
		*output = MH_NET_WIRE_31_LOBBY;
	else
		return 0;

	return 1;
}

int
mh_net_supported(unsigned int type)
{
	return type >= MH_NET_JOIN && type <= MH_NET_KICK;
}

size_t
mh_net_wire_limit(enum mh_net_wire wire)
{
	if (wire == MH_NET_WIRE_F12)
		return MH_NET_PACKET_LIMIT;

	if (wire == MH_NET_WIRE_31_LOBBY)
		return MH_NET_WIRE_PACKET_LIMIT;

	return 0;
}

int
mh_net_wire_supported(enum mh_net_wire wire, unsigned int type)
{
	if (wire == MH_NET_WIRE_F12)
		return mh_net_supported(type);

	if (wire != MH_NET_WIRE_31_LOBBY)
		return 0;

	return (type >= MH_NET_JOIN && type <= MH_NET_RACE_CONFIG) ||
		type == MH_NET_PING || type == MH_NET_LOBBY_PONG ||
		type == MH_NET_PONG || type == MH_NET_START ||
		type == MH_NET_CAR || type == MH_NET_CAR_FAR ||
		type == MH_NET_STATUS || type == MH_NET_LEAVE_LOBBY ||
		type == MH_NET_LEAVE_RACE || type == MH_NET_KICK ||
		type == MH_NET_LAST_RACE || type == MH_NET_LOADED;
}

static unsigned int
wire_players(enum mh_net_wire wire)
{
	return wire == MH_NET_WIRE_F12 ? MH_NET_PLAYERS : MH_NET_WIRE_PLAYERS;
}

static size_t
roster_size(enum mh_net_wire wire)
{
	return wire == MH_NET_WIRE_F12 ? ROSTER_BYTES : PATCHED_ROSTER_BYTES;
}

static int
profile_widths_valid(const struct mh_net_profile *profile)
{
	unsigned int i;

	if (profile->flags > WIRE_BYTE_MAX ||
		profile->update_rate > WIRE_BYTE_MAX ||
		profile->gear_mode > WIRE_BYTE_MAX ||
		profile->value_a > WIRE_BYTE_MAX ||
		profile->value_b > WIRE_BYTE_MAX ||
		profile->control_type > WIRE_BYTE_MAX)
		return 0;

	for (i = 0; i < MH_NET_PROFILE_VALUES; i++)
		if (profile->values[i] > MH_U32_MAX)
			return 0;

	return 1;
}

static int
profile_valid(const struct mh_net_profile *profile)
{
	return profile_widths_valid(profile) &&
		memchr(profile->name, 0, sizeof(profile->name)) != NULL &&
		memchr(profile->team, 0, sizeof(profile->team)) != NULL &&
		memchr(profile->car, 0, sizeof(profile->car)) != NULL &&
		memchr(profile->horn, 0, sizeof(profile->horn)) != NULL;
}

static int
roster_valid(enum mh_net_wire wire, const struct mh_net_roster *roster)
{
	unsigned int i;

	if (memchr(roster->track, 0, sizeof(roster->track)) == NULL ||
		roster->mode > WIRE_BYTE_MAX || roster->laps > WIRE_BYTE_MAX ||
		roster->player_count == 0 ||
		roster->player_count > wire_players(wire) ||
		roster->car_count > WIRE_BYTE_MAX)
		return 0;

	for (i = 0; i < wire_players(wire); i++) {
		if (!profile_widths_valid(&roster->players[i]))
			return 0;

		if (i < roster->player_count &&
			!profile_valid(&roster->players[i]))
			return 0;
	}

	return 1;
}

static void
profile_read(const unsigned char *data, struct mh_net_profile *profile)
{
	unsigned int i;

	profile->flags = data[PROFILE_FLAGS];
	profile->update_rate = data[PROFILE_RATE];
	memcpy(profile->name, data + PROFILE_NAME, sizeof(profile->name));
	memcpy(profile->team, data + PROFILE_TEAM, sizeof(profile->team));
	profile->gear_mode = data[PROFILE_GEAR];
	memcpy(profile->car, data + PROFILE_CAR, sizeof(profile->car));
	memcpy(profile->horn, data + PROFILE_HORN, sizeof(profile->horn));

	for (i = 0; i < MH_NET_PROFILE_VALUES; i++)
		profile->values[i] =
			mh_read_u32le(data + PROFILE_VALUES + 4 * i);

	profile->value_a = data[PROFILE_VALUE_A];
	profile->value_b = data[PROFILE_VALUE_B];
	profile->control_type = data[PROFILE_CONTROL];
}

static void
profile_write(unsigned char *data, const struct mh_net_profile *profile)
{
	unsigned int i;

	data[PROFILE_FLAGS] = (unsigned char)profile->flags;
	data[PROFILE_RATE] = (unsigned char)profile->update_rate;
	memcpy(data + PROFILE_NAME, profile->name, sizeof(profile->name));
	memcpy(data + PROFILE_TEAM, profile->team, sizeof(profile->team));
	data[PROFILE_GEAR] = (unsigned char)profile->gear_mode;
	memcpy(data + PROFILE_CAR, profile->car, sizeof(profile->car));
	memcpy(data + PROFILE_HORN, profile->horn, sizeof(profile->horn));

	for (i = 0; i < MH_NET_PROFILE_VALUES; i++)
		mh_write_u32le(
			data + PROFILE_VALUES + 4 * i, profile->values[i]);

	data[PROFILE_VALUE_A] = (unsigned char)profile->value_a;
	data[PROFILE_VALUE_B] = (unsigned char)profile->value_b;
	data[PROFILE_CONTROL] = (unsigned char)profile->control_type;
}

static void
roster_read(enum mh_net_wire wire, const unsigned char *data,
	struct mh_net_roster *roster)
{
	unsigned int i;

	memcpy(roster->track, data + ROSTER_TRACK, sizeof(roster->track));
	roster->mode = data[ROSTER_MODE];
	roster->laps = data[ROSTER_LAPS];
	roster->player_count = data[ROSTER_PLAYERS];
	roster->car_count = data[ROSTER_CARS];

	for (i = 0; i < wire_players(wire); i++)
		profile_read(data + ROSTER_PROFILES + PROFILE_BYTES * i,
			&roster->players[i]);
}

static void
roster_write(enum mh_net_wire wire, unsigned char *data,
	const struct mh_net_roster *roster)
{
	unsigned int i;

	memcpy(data + ROSTER_TRACK, roster->track, sizeof(roster->track));
	data[ROSTER_MODE] = (unsigned char)roster->mode;
	data[ROSTER_LAPS] = (unsigned char)roster->laps;
	data[ROSTER_PLAYERS] = (unsigned char)roster->player_count;
	data[ROSTER_CARS] = (unsigned char)roster->car_count;

	for (i = 0; i < wire_players(wire); i++)
		profile_write(data + ROSTER_PROFILES + PROFILE_BYTES * i,
			&roster->players[i]);
}

static void
status_read(const unsigned char *data, struct mh_net_status *status)
{
	status->flags = data[STATUS_FLAGS];
	status->ping_ms = mh_read_u32le(data + STATUS_PING);
	status->value_a = data[STATUS_VALUE_A];
	status->value_b = data[STATUS_VALUE_B];
}

static enum mh_error_code
status_write(unsigned char *data, const struct mh_net_status *status,
	struct mh_error *error_out)
{
	if (status->flags > WIRE_BYTE_MAX || status->ping_ms > MH_U32_MAX ||
		status->value_a > WIRE_BYTE_MAX ||
		status->value_b > WIRE_BYTE_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"network status exceeds wire width");

	data[STATUS_FLAGS] = (unsigned char)status->flags;
	mh_write_u32le(data + STATUS_PING, status->ping_ms);
	data[STATUS_VALUE_A] = (unsigned char)status->value_a;
	data[STATUS_VALUE_B] = (unsigned char)status->value_b;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
statistics_read(const unsigned char *data, struct mh_net_statistics_row *row)
{
	memcpy(row->name, data, sizeof(row->name));
	row->color = mh_read_u32le(data + STATISTICS_COLOR);
	row->position = data[STATISTICS_POSITION];
	row->top_speed_kmh =
		(unsigned int)mh_read_u16le(data + STATISTICS_SPEED);
	row->best_lap_ms = mh_read_u32le(data + STATISTICS_BEST_LAP);
	row->race_ms = mh_read_u32le(data + STATISTICS_RACE_TIME);
}

static enum mh_error_code
statistics_write(unsigned char *data, const struct mh_net_statistics_row *row,
	struct mh_error *error_out)
{
	if (row->color > MH_U32_MAX || row->position > WIRE_BYTE_MAX ||
		row->top_speed_kmh > MH_U16_MAX ||
		row->best_lap_ms > MH_U32_MAX || row->race_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"network statistics exceed wire width");

	memcpy(data, row->name, sizeof(row->name));
	mh_write_u32le(data + STATISTICS_COLOR, row->color);
	data[STATISTICS_POSITION] = (unsigned char)row->position;
	mh_write_u16le(data + STATISTICS_SPEED, row->top_speed_kmh);
	mh_write_u32le(data + STATISTICS_BEST_LAP, row->best_lap_ms);
	mh_write_u32le(data + STATISTICS_RACE_TIME, row->race_ms);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_wire_decode(enum mh_net_wire wire, const struct mh_net_frame *frame,
	struct mh_net_message *output, struct mh_error *error_out)
{
	struct mh_net_message message;
	struct mh_net_frame actual;
	struct mh_net_vehicle vehicle;
	const unsigned char *data, *terminator;
	enum mh_error_code status;
	size_t expected_size, i;
	double version;

	if (frame == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing network message");

	status = mh_net_frame_read(
		wire, frame->data, frame->size, &actual, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (actual.size != frame->size || actual.type != frame->type)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"inconsistent network frame");

	if (!mh_net_wire_supported(wire, actual.type))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported network message type");

	data = actual.data;
	memset(&message, 0, sizeof(message));
	message.type = actual.type;
	expected_size = 0;

	switch (message.type) {
	case MH_NET_JOIN:
		message.join.has_proof =
			actual.size == JOIN_PASSWORD_OFFSET + MH_SHA256_BYTES;
		expected_size =
			message.join.has_proof ? actual.size : JOIN_BYTES;
		break;

	case MH_NET_ACCEPT:
		expected_size = wire == MH_NET_WIRE_31_LOBBY &&
				actual.size == ACCEPT_TIMING_BYTES
			? ACCEPT_TIMING_BYTES
			: PLAYER_BYTES;
		if (expected_size == ACCEPT_TIMING_BYTES) {
			if (mh_read_u32le(data + ACCEPT_TIMING_VERSION) !=
					MH_SIM_TIMING_VERSION ||
				!mh_sim_rate_valid(mh_read_u32le(
					data + ACCEPT_SIMULATION_RATE)))
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"unsupported server simulation timing");

			message.timing_version = MH_SIM_TIMING_VERSION;
			message.simulation_rate = (unsigned int)mh_read_u32le(
				data + ACCEPT_SIMULATION_RATE);
		}

		break;

	case MH_NET_REJECT:
		if (actual.size < REJECT_MIN_BYTES ||
			memchr(data + REJECT_TEXT_OFFSET, 0,
				actual.size - REJECT_TEXT_OFFSET) !=
				data + actual.size - 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network rejection text");

		message.reason = data[REJECT_REASON_OFFSET];
		memcpy(message.text, data + REJECT_TEXT_OFFSET,
			actual.size - REJECT_TEXT_OFFSET);
		break;

	case MH_NET_PROFILE_JOIN:
	case MH_NET_PROFILE_UPDATE:
		expected_size = PROFILE_MESSAGE_BYTES;
		break;

	case MH_NET_ROSTER:
	case MH_NET_RACE_CONFIG:
		expected_size = roster_size(wire);
		break;

	case MH_NET_CHAT:
	case MH_NET_NOTICE:
		if (actual.size < MH_NET_CHAT_OVERHEAD ||
			data[PLAYER_OFFSET] >= wire_players(wire))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network chat");

		terminator =
			(const unsigned char *)memchr(data + CHAT_TEXT_OFFSET,
				0, actual.size - CHAT_TEXT_OFFSET);

		if (terminator != data + actual.size - 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network chat terminator");

		message.player = data[PLAYER_OFFSET];
		message.color = mh_read_u32le(data + CHAT_COLOR_OFFSET);
		memcpy(message.text, data + CHAT_TEXT_OFFSET,
			actual.size - CHAT_TEXT_OFFSET);
		break;

	case MH_NET_LOADED:
		expected_size = PLAYER_BYTES;
		break;

	case MH_NET_PING:
		expected_size = PING_BYTES;
		break;

	case MH_NET_LOBBY_PONG:
	case MH_NET_PONG:
		expected_size = PONG_BYTES;
		break;

	case MH_NET_CAR:
		expected_size = CAR_BYTES;
		break;

	case MH_NET_CAR_FAR:
		expected_size = CAR_FAR_BYTES;
		break;

	case MH_NET_STATUS:
		expected_size =
			MH_NET_HEADER_BYTES + STATUS_BYTES * wire_players(wire);
		break;

	case MH_NET_LAST_RACE:
		expected_size = STATISTICS_BYTES;
		break;

	case MH_NET_START:
	case MH_NET_KICK:
		expected_size = MH_NET_HEADER_BYTES;
		break;

	case MH_NET_LEAVE_LOBBY:
	case MH_NET_LEAVE_RACE:
		expected_size = PLAYER_BYTES;
		break;
	}

	if (expected_size != 0 && actual.size != expected_size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incorrect network payload size");

	if (message.type == MH_NET_JOIN) {
		if (!mh_read_f32le(data + JOIN_VERSION_OFFSET, &version) ||
			(!message.join.has_proof &&
				memchr(data + JOIN_PASSWORD_OFFSET, 0,
					sizeof(message.join.password)) == NULL))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network join request");

		message.join.provider = data[JOIN_PROVIDER_OFFSET];
		message.join.version_bits =
			mh_read_u32le(data + JOIN_VERSION_OFFSET);

		if (message.join.has_proof)
			memcpy(message.join.proof, data + JOIN_PASSWORD_OFFSET,
				MH_SHA256_BYTES);
		else
			memcpy(message.join.password,
				data + JOIN_PASSWORD_OFFSET,
				sizeof(message.join.password));
	}

	if (message.type == MH_NET_PING)
		message.stamp = mh_read_u32le(data + PING_STAMP_OFFSET);

	if (message.type == MH_NET_ROSTER ||
		message.type == MH_NET_RACE_CONFIG) {
		roster_read(wire, data, &message.roster);

		if (!roster_valid(wire, &message.roster))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network roster");
	}

	if (message.type == MH_NET_ACCEPT ||
		message.type == MH_NET_PROFILE_JOIN ||
		message.type == MH_NET_PROFILE_UPDATE ||
		message.type == MH_NET_LOBBY_PONG ||
		message.type == MH_NET_PONG || message.type == MH_NET_CAR ||
		message.type == MH_NET_CAR_FAR ||
		message.type == MH_NET_LEAVE_LOBBY ||
		message.type == MH_NET_LEAVE_RACE ||
		message.type == MH_NET_LOADED) {
		message.player = data[PLAYER_OFFSET];

		if (message.player >= wire_players(wire))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network player index");
	}

	if (message.type == MH_NET_PROFILE_JOIN ||
		message.type == MH_NET_PROFILE_UPDATE) {
		profile_read(data + PROFILE_OFFSET, &message.profile);

		if (!profile_valid(&message.profile))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network player profile");
	}

	if (message.type == MH_NET_LOBBY_PONG || message.type == MH_NET_PONG ||
		message.type == MH_NET_CAR_FAR)
		message.stamp = mh_read_u32le(data + PONG_STAMP_OFFSET);

	if (message.type == MH_NET_CAR) {
		/*
		 * Host forwarding leaves byte 5 untouched in its shared buffer.
		 * Preserve it here; interpreting it as an index requires role
		 * checks.
		 */
		message.view_player = data[CAR_VIEW_OFFSET];
		message.stamp = mh_read_u32le(data + CAR_STAMP_OFFSET);
		memcpy(message.car, data + CAR_BODY_OFFSET,
			MH_NET_VEHICLE_BYTES);
		status = mh_net_vehicle_decode(
			message.car, MH_NET_VEHICLE_BYTES, &vehicle, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (message.type == MH_NET_CAR_FAR)
		memcpy(message.car, data + FAR_BODY_OFFSET,
			MH_NET_VEHICLE_FAR_BYTES);

	if (message.type == MH_NET_STATUS)
		for (i = 0; i < wire_players(wire); i++)
			status_read(data + STATUS_OFFSET + STATUS_BYTES * i,
				&message.status[i]);

	if (message.type == MH_NET_LAST_RACE)
		for (i = 0; i < MH_NET_STATISTICS_ROWS; i++)
			statistics_read(data + MH_NET_HEADER_BYTES +
					STATISTICS_ROW_BYTES * i,
				&message.statistics.rows[i]);

	*output = message;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_wire_encode(enum mh_net_wire wire, const struct mh_net_message *message,
	unsigned char *output, size_t capacity, size_t *written,
	struct mh_error *error_out)
{
	unsigned char data[MH_NET_WIRE_PACKET_LIMIT];
	struct mh_net_frame frame;
	struct mh_net_message checked;
	enum mh_error_code status;
	const char *terminator;
	size_t size, i;

	if (message == NULL || output == NULL || written == NULL ||
		mh_net_wire_limit(wire) == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network encoder arguments");

	if (!mh_net_wire_supported(wire, message->type))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported network message type");

	if (message->player > WIRE_BYTE_MAX ||
		message->view_player > WIRE_BYTE_MAX ||
		message->stamp > MH_U32_MAX || message->color > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"network field exceeds wire width");

	memset(data, 0, sizeof(data));
	size = 0;

	switch (message->type) {
	case MH_NET_JOIN:
		if (message->join.provider > WIRE_BYTE_MAX ||
			message->join.version_bits > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"network join field exceeds wire width");

		size = message->join.has_proof
			? JOIN_PASSWORD_OFFSET + MH_SHA256_BYTES
			: JOIN_BYTES;
		data[JOIN_PROVIDER_OFFSET] =
			(unsigned char)message->join.provider;
		mh_write_u32le(
			data + JOIN_VERSION_OFFSET, message->join.version_bits);

		if (message->join.has_proof)
			memcpy(data + JOIN_PASSWORD_OFFSET, message->join.proof,
				MH_SHA256_BYTES);
		else
			memcpy(data + JOIN_PASSWORD_OFFSET,
				message->join.password,
				sizeof(message->join.password));
		break;

	case MH_NET_ACCEPT:
		size = PLAYER_BYTES;
		data[PLAYER_OFFSET] = (unsigned char)message->player;

		if (message->timing_version || message->simulation_rate) {
			if (wire != MH_NET_WIRE_31_LOBBY ||
				message->timing_version !=
					MH_SIM_TIMING_VERSION ||
				!mh_sim_rate_valid(message->simulation_rate))
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"unsupported server simulation timing");

			size = ACCEPT_TIMING_BYTES;
			mh_write_u32le(data + ACCEPT_TIMING_VERSION,
				message->timing_version);
			mh_write_u32le(data + ACCEPT_SIMULATION_RATE,
				message->simulation_rate);
		}

		break;

	case MH_NET_REJECT:
		terminator = (const char *)memchr(
			message->text, 0, sizeof(message->text));
		if (terminator == NULL || message->reason > WIRE_BYTE_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network rejection");

		size = REJECT_MIN_BYTES + (size_t)(terminator - message->text);

		if (size > mh_net_wire_limit(wire))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"network rejection text too long");

		data[REJECT_REASON_OFFSET] = (unsigned char)message->reason;
		memcpy(data + REJECT_TEXT_OFFSET, message->text,
			size - REJECT_TEXT_OFFSET);
		break;

	case MH_NET_PROFILE_JOIN:
	case MH_NET_PROFILE_UPDATE:
		if (!profile_valid(&message->profile))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network player profile");

		size = PROFILE_MESSAGE_BYTES;
		data[PLAYER_OFFSET] = (unsigned char)message->player;
		profile_write(data + PROFILE_OFFSET, &message->profile);
		break;

	case MH_NET_ROSTER:
	case MH_NET_RACE_CONFIG:
		if (!roster_valid(wire, &message->roster))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network roster");

		size = roster_size(wire);
		roster_write(wire, data, &message->roster);

		break;

	case MH_NET_CHAT:
	case MH_NET_NOTICE:
		terminator = (const char *)memchr(
			message->text, 0, sizeof(message->text));
		if (terminator == NULL)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"network text too long");

		size = MH_NET_CHAT_OVERHEAD +
			(size_t)(terminator - message->text);

		if (size > mh_net_wire_limit(wire))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"network text too long");

		data[PLAYER_OFFSET] = (unsigned char)message->player;
		mh_write_u32le(data + CHAT_COLOR_OFFSET, message->color);
		memcpy(data + CHAT_TEXT_OFFSET, message->text,
			size - CHAT_TEXT_OFFSET);
		break;

	case MH_NET_LOADED:
		if (message->player >= wire_players(wire))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid loaded player");

		size = PLAYER_BYTES;
		data[PLAYER_OFFSET] = (unsigned char)message->player;
		break;

	case MH_NET_PING:
		size = PING_BYTES;
		mh_write_u32le(data + PING_STAMP_OFFSET, message->stamp);
		break;

	case MH_NET_LOBBY_PONG:
	case MH_NET_PONG:
		size = PONG_BYTES;
		data[PLAYER_OFFSET] = (unsigned char)message->player;
		mh_write_u32le(data + PONG_STAMP_OFFSET, message->stamp);
		break;

	case MH_NET_CAR:
		size = CAR_BYTES;
		data[PLAYER_OFFSET] = (unsigned char)message->player;
		data[CAR_VIEW_OFFSET] = (unsigned char)message->view_player;
		mh_write_u32le(data + CAR_STAMP_OFFSET, message->stamp);
		memcpy(data + CAR_BODY_OFFSET, message->car,
			MH_NET_VEHICLE_BYTES);
		break;

	case MH_NET_CAR_FAR:
		size = CAR_FAR_BYTES;
		data[PLAYER_OFFSET] = (unsigned char)message->player;
		mh_write_u32le(data + PONG_STAMP_OFFSET, message->stamp);
		memcpy(data + FAR_BODY_OFFSET, message->car,
			MH_NET_VEHICLE_FAR_BYTES);
		break;

	case MH_NET_STATUS:
		size = MH_NET_HEADER_BYTES + STATUS_BYTES * wire_players(wire);

		for (i = 0; i < wire_players(wire); i++) {
			status = status_write(
				data + STATUS_OFFSET + STATUS_BYTES * i,
				&message->status[i], error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		break;

	case MH_NET_LAST_RACE:
		size = STATISTICS_BYTES;

		for (i = 0; i < MH_NET_STATISTICS_ROWS; i++) {
			status = statistics_write(data + MH_NET_HEADER_BYTES +
					STATISTICS_ROW_BYTES * i,
				&message->statistics.rows[i], error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		break;

	case MH_NET_START:
	case MH_NET_KICK:
		size = MH_NET_HEADER_BYTES;
		break;

	case MH_NET_LEAVE_LOBBY:
	case MH_NET_LEAVE_RACE:
		size = PLAYER_BYTES;
		data[PLAYER_OFFSET] = (unsigned char)message->player;
		break;
	}

	mh_write_u16le(data, message->type);
	mh_write_u16le(data + MH_NET_LENGTH_OFFSET, (unsigned long)size);
	frame.data = data;
	frame.size = size;
	frame.type = message->type;
	status = mh_net_wire_decode(wire, &frame, &checked, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (size > capacity)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"network output buffer too small");

	memcpy(output, data, size);
	*written = size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_decode(const struct mh_net_frame *frame, struct mh_net_message *output,
	struct mh_error *error_out)
{
	return mh_net_wire_decode(MH_NET_WIRE_F12, frame, output, error_out);
}

enum mh_error_code
mh_net_encode(const struct mh_net_message *message, unsigned char *output,
	size_t capacity, size_t *written, struct mh_error *error_out)
{
	return mh_net_wire_encode(
		MH_NET_WIRE_F12, message, output, capacity, written, error_out);
}
