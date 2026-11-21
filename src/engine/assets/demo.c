#include <string.h>

#include "engine/assets/demo.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/simulation_rate.h"

#define VERSION_25_BITS 0x40200000UL
#define VERSION_30_BITS 0x40400000UL
#define VERSION_40_BITS 0x40800000UL

enum {
	HEADER_FLAGS = 4,
	HEADER_TRACK = 8,
	HEADER_LAPS = 24,
	HEADER_PLAYER_COUNT = 28,
	HEADER_LOCAL_PLAYER = 32,
	HEADER_PLAYERS = 36,
	PLAYER_NAME = 4,
	PLAYER_TEAM = 20,
	PLAYER_CAR = 25,
	PLAYER_HORN = 41,
	/* Relative to the version-dependent color block. */
	PLAYER_GEAR_AFTER_COLORS = 16,
	PLAYER_CONTROL_AFTER_COLORS = 20,
	CROSSING_ROW_BYTES = 16,
	CROSSING_TABLE_BYTES = 432,
	INITIAL_FRAMES = 64,
	FRAME_TIME = 4
};

/* Native extension follows the 3.0 roster, before the crossing tables. */
#define HEADER_TIMING_VERSION 1089
#define HEADER_SIMULATION_RATE 1093

struct demo_layout {
	unsigned long	version_bits;
	size_t		header_bytes;
	unsigned int	slots, player_bytes, crossings, colors;
};

static const struct demo_layout layout_25 = {
	VERSION_25_BITS, MH_DEMO_HEADER_BYTES, MH_DEMO_PLAYERS, 65, 556, 41};
static const struct demo_layout layout_30 = {VERSION_30_BITS,
	MH_DEMO_30_HEADER_BYTES, MH_DEMO_MAX_PLAYERS, 81, 1089, 57};
static const struct demo_layout layout_40 = {VERSION_40_BITS,
	MH_DEMO_MAX_HEADER_BYTES, MH_DEMO_MAX_PLAYERS, 81, 1097, 57};

static const struct demo_layout *
demo_layout(enum mh_demo_version version)
{
	switch (version) {
	case MH_DEMO_VERSION_25:
		return &layout_25;
	case MH_DEMO_VERSION_30:
		return &layout_30;
	case MH_DEMO_VERSION_40:
		return &layout_40;
	default:
		return NULL;
	}
}

size_t
mh_demo_header_size(enum mh_demo_version version)
{
	const struct demo_layout *layout;

	layout = demo_layout(version);

	return layout == NULL ? 0 : layout->header_bytes;
}

unsigned int
mh_demo_slots(enum mh_demo_version version)
{
	const struct demo_layout *layout;

	layout = demo_layout(version);

	return layout == NULL ? 0 : layout->slots;
}

size_t
mh_demo_crossings_offset(enum mh_demo_version version)
{
	const struct demo_layout *layout;

	layout = demo_layout(version);

	return layout == NULL ? 0 : layout->crossings;
}

static enum mh_error_code
write_player(unsigned char *data, const struct demo_layout *layout,
	enum mh_demo_version version, unsigned int slot,
	const struct mh_demo_player *player, struct mh_error *error_out)
{
	unsigned char *record;
	unsigned int i, j;

	record = data + HEADER_PLAYERS + layout->player_bytes * slot;

	if (player->active > MH_U32_MAX || player->gear_mode > MH_U32_MAX ||
		player->control_type > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"demo player exceeds disk word");

	mh_write_u32le(record, player->active);
	memcpy(record + PLAYER_NAME, player->name, MH_DEMO_NAME_BYTES);
	memcpy(record + PLAYER_TEAM, player->team, MH_DEMO_TEAM_BYTES);
	memcpy(record + PLAYER_CAR, player->car, MH_DEMO_NAME_BYTES);

	if (version != MH_DEMO_VERSION_25)
		memcpy(record + PLAYER_HORN, player->horn, MH_DEMO_NAME_BYTES);

	for (i = 0; i < 4; i++) {
		if (player->colors[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"demo color exceeds disk word");

		mh_write_u32le(
			record + layout->colors + 4 * i, player->colors[i]);
	}

	mh_write_u32le(record + layout->colors + PLAYER_GEAR_AFTER_COLORS,
		player->gear_mode);
	mh_write_u32le(record + layout->colors + PLAYER_CONTROL_AFTER_COLORS,
		player->control_type);

	for (i = 0; i < MH_DEMO_CROSSING_ROWS; i++)
		for (j = 0; j < 4; j++) {
			if (player->crossings[i][j] > MH_U32_MAX)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"demo crossing exceeds disk word");

			mh_write_u32le(data + layout->crossings +
					CROSSING_TABLE_BYTES * slot +
					CROSSING_ROW_BYTES * i + 4 * j,
				player->crossings[i][j]);
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
read_player(const unsigned char *data, const struct demo_layout *layout,
	enum mh_demo_version version, unsigned int slot,
	struct mh_demo_player *player)
{
	const unsigned char *record;
	unsigned int i, j;

	record = data + HEADER_PLAYERS + layout->player_bytes * slot;
	player->active = mh_read_u32le(record);

	memcpy(player->name, record + PLAYER_NAME, MH_DEMO_NAME_BYTES);
	memcpy(player->team, record + PLAYER_TEAM, MH_DEMO_TEAM_BYTES);
	memcpy(player->car, record + PLAYER_CAR, MH_DEMO_NAME_BYTES);

	if (version != MH_DEMO_VERSION_25)
		memcpy(player->horn, record + PLAYER_HORN, MH_DEMO_NAME_BYTES);

	for (i = 0; i < 4; i++)
		player->colors[i] =
			mh_read_u32le(record + layout->colors + 4 * i);

	player->gear_mode = mh_read_u32le(
		record + layout->colors + PLAYER_GEAR_AFTER_COLORS);
	player->control_type = mh_read_u32le(
		record + layout->colors + PLAYER_CONTROL_AFTER_COLORS);

	for (i = 0; i < MH_DEMO_CROSSING_ROWS; i++)
		for (j = 0; j < 4; j++)
			player->crossings[i][j] =
				mh_read_u32le(data + layout->crossings +
					CROSSING_TABLE_BYTES * slot +
					CROSSING_ROW_BYTES * i + 4 * j);
}

enum mh_error_code
mh_demo_header_write(const struct mh_demo *demo, unsigned char *data,
	size_t size, struct mh_error *error_out)
{
	unsigned char result[MH_DEMO_MAX_HEADER_BYTES];
	const struct demo_layout *layout;
	const struct mh_demo_player *player;
	unsigned int i, active_count;
	enum mh_error_code status;

	layout = demo == NULL ? NULL : demo_layout(demo->version);

	if (layout == NULL || data == NULL || size < layout->header_bytes ||
		demo->player_count < 1 || demo->player_count > layout->slots ||
		demo->local_player >= layout->slots ||
		demo->flags > MH_U32_MAX || demo->laps > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo header output");

	memset(result, 0, sizeof(result));

	if (demo->version == MH_DEMO_VERSION_40) {
		if (demo->timing_version != MH_SIM_TIMING_VERSION ||
			!mh_sim_rate_valid(demo->simulation_rate))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported recording simulation timing");

		mh_write_u32le(
			result + HEADER_TIMING_VERSION, demo->timing_version);
		mh_write_u32le(
			result + HEADER_SIMULATION_RATE, demo->simulation_rate);
	} else if (demo->timing_version != 0 || demo->simulation_rate != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"legacy recording cannot store simulation timing");

	mh_write_u32le(result, layout->version_bits);
	mh_write_u32le(result + HEADER_FLAGS, demo->flags);
	memcpy(result + HEADER_TRACK, demo->track, MH_DEMO_NAME_BYTES);
	mh_write_u32le(result + HEADER_LAPS, demo->laps);
	mh_write_u32le(result + HEADER_PLAYER_COUNT, demo->player_count);
	mh_write_u32le(result + HEADER_LOCAL_PLAYER, demo->local_player);
	active_count = 0;

	for (i = 0; i < layout->slots; i++) {
		player = &demo->players[i];
		status = write_player(
			result, layout, demo->version, i, player, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (player->active)
			active_count++;
	}

	if (active_count != demo->player_count ||
		!demo->players[demo->local_player].active)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"inconsistent demo output roster");

	/*
	 * Do not silently discard extra entrants when writing the old format.
	 */
	for (i = layout->slots; i < MH_DEMO_MAX_PLAYERS; i++)
		if (demo->players[i].active)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"demo player outside version capacity");

	memcpy(data, result, layout->header_bytes);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_read(const unsigned char *data, size_t size, struct mh_demo *output,
	struct mh_error *error_out)
{
	struct mh_demo result;
	const struct demo_layout *layout;
	enum mh_demo_version version;
	struct mh_demo_player *player;
	unsigned long player_count, local_slot;
	unsigned int i, active_count;
	size_t payload_bytes;

	if (data == NULL || output == NULL || size < 4)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "truncated demo header");

	switch (mh_read_u32le(data)) {
	case VERSION_25_BITS:
		version = MH_DEMO_VERSION_25;
		break;

	case VERSION_30_BITS:
		version = MH_DEMO_VERSION_30;
		break;

	case VERSION_40_BITS:
		version = MH_DEMO_VERSION_40;
		break;

	default:
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported demo version");
	}

	layout = demo_layout(version);

	if (size < layout->header_bytes)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "truncated demo header");

	player_count = mh_read_u32le(data + HEADER_PLAYER_COUNT);
	local_slot = mh_read_u32le(data + HEADER_LOCAL_PLAYER);

	if (player_count < 1 || player_count > layout->slots ||
		local_slot >= layout->slots)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo player count or local slot");

	memset(&result, 0, sizeof(result));
	result.version = version;

	if (version == MH_DEMO_VERSION_40) {
		if (mh_read_u32le(data + HEADER_TIMING_VERSION) !=
				MH_SIM_TIMING_VERSION ||
			!mh_sim_rate_valid(
				mh_read_u32le(data + HEADER_SIMULATION_RATE)))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported recording simulation timing");

		result.timing_version = MH_SIM_TIMING_VERSION;
		result.simulation_rate = (unsigned int)mh_read_u32le(
			data + HEADER_SIMULATION_RATE);
	}

	result.data = data;
	result.size = size;
	result.flags = mh_read_u32le(data + HEADER_FLAGS);
	result.laps = mh_read_u32le(data + HEADER_LAPS);
	result.player_count = (unsigned int)player_count;
	result.local_player = (unsigned int)local_slot;
	memcpy(result.track, data + HEADER_TRACK, MH_DEMO_NAME_BYTES);
	result.frame_bytes = MH_DEMO_FRAME_HEADER_BYTES +
		MH_DEMO_VEHICLE_BYTES * result.player_count;
	payload_bytes = size - layout->header_bytes;

	if (payload_bytes % result.frame_bytes != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "truncated demo frame");

	result.frame_count = payload_bytes / result.frame_bytes;

	if (result.frame_count < INITIAL_FRAMES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"demo needs an initial 64-frame block");

	active_count = 0;

	for (i = 0; i < layout->slots; i++) {
		player = &result.players[i];
		read_player(data, layout, version, i, player);

		if (player->active != 0)
			active_count++;
	}

	if (active_count != result.player_count ||
		result.players[result.local_player].active == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"inconsistent demo roster");

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_frame_read(const struct mh_demo *demo, size_t index,
	struct mh_demo_frame *output, struct mh_error *error_out)
{
	struct mh_demo_frame result;
	const struct demo_layout *layout;
	unsigned long mask;
	const unsigned char *frame;
	size_t stride, vehicle_offset;
	unsigned int i;

	layout = demo == NULL ? NULL : demo_layout(demo->version);

	if (layout == NULL || output == NULL || demo->data == NULL ||
		demo->size < layout->header_bytes || demo->player_count < 1 ||
		demo->player_count > layout->slots)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid demo view");

	stride = MH_DEMO_FRAME_HEADER_BYTES +
		MH_DEMO_VEHICLE_BYTES * demo->player_count;

	if (demo->frame_bytes != stride || index >= demo->frame_count ||
		index >= (demo->size - layout->header_bytes) / stride)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "demo frame out of range");

	/*
	 * The division bound above proves both the multiply and complete frame.
	 */
	frame = demo->data + layout->header_bytes + index * stride;
	memset(&result, 0, sizeof(result));
	result.flags = mh_read_u32le(frame);
	result.milliseconds = mh_read_u32le(frame + FRAME_TIME);
	mask = (1UL << layout->slots) - 1;
	result.active_mask = (unsigned int)(result.flags & mask);
	result.unavailable_mask =
		(unsigned int)((result.flags >> layout->slots) & mask);
	vehicle_offset = MH_DEMO_FRAME_HEADER_BYTES;

	for (i = 0; i < layout->slots; i++)
		if (demo->players[i].active != 0) {
			if (vehicle_offset > stride - MH_DEMO_VEHICLE_BYTES)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"inconsistent demo frame roster");

			result.vehicles[i] = frame + vehicle_offset;
			vehicle_offset += MH_DEMO_VEHICLE_BYTES;
		}

	if (vehicle_offset != stride)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"inconsistent demo frame roster");

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
