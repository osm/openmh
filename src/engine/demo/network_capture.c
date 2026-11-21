#include <string.h>

#include "engine/core/error.h"
#include "engine/core/simulation_rate.h"
#include "engine/core/text.h"
#include "engine/demo/network_capture.h"
#include "engine/game/race_limits.h"
#include "engine/network/race_internal.h"

static int
valid_network_race(const struct mh_net_race *race)
{
	const struct mh_field *field;
	unsigned int entry, slot, mapped_slots;

	if (race == NULL)
		return 0;

	field = &race->field;

	if (!field->network || field->playback || field->count < 1 ||
		field->count > MH_DEMO_MAX_PLAYERS ||
		field->count != race->layout.count ||
		!(field->active_mask & 1) ||
		field->active_mask >= (1U << field->count) ||
		field->unavailable_mask >= (1U << field->count) ||
		(field->tuning.catchup != 0 && field->tuning.catchup != 1))
		return 0;

	mapped_slots = 0;

	for (entry = 0; entry < field->count; entry++) {
		slot = race->layout.player[entry];

		if (slot >= MH_DEMO_MAX_PLAYERS ||
			(mapped_slots & (1U << slot)) ||
			race->layout.entry[slot] != entry ||
			field->cars[entry].remote != (entry != 0) ||
			field->cars[entry].car == NULL ||
			field->cars[entry].track == NULL ||
			field->cars[entry].track != field->cars[0].track ||
			!field->cars[entry].countdown ||
			field->cars[entry].race.lap_limit !=
				field->cars[0].track->laps)
			return 0;

		mapped_slots |= 1U << slot;
	}

	for (slot = 0; slot < MH_DEMO_MAX_PLAYERS; slot++)
		if (!(mapped_slots & (1U << slot)) &&
			race->layout.entry[slot] != MH_NET_WIRE_PLAYERS)
			return 0;

	return field->cars[0].track->laps >= MH_RACE_MIN_LAPS &&
		field->cars[0].track->laps <= MH_RACE_MAX_LAPS;
}

static void
copy_identity_bytes(char *output, const char *source, size_t width)
{
	size_t i;

	/*
	 * The enclosing header is zeroed, matching the original byte copy up
	 * to and including NUL. Raw high bytes remain valid player-name data.
	 */
	for (i = 0; i < width && source[i] != 0; i++)
		output[i] = source[i];
}

static void
copy_player_identity(
	struct mh_demo_player *player, const struct mh_net_profile *profile)
{
	copy_identity_bytes(player->name, profile->name, MH_DEMO_NAME_BYTES);

	copy_identity_bytes(player->team, profile->team, MH_DEMO_TEAM_BYTES);
	copy_identity_bytes(player->car, profile->car, MH_DEMO_NAME_BYTES);
	copy_identity_bytes(player->horn, profile->horn, MH_DEMO_NAME_BYTES);
	memcpy(player->colors, profile->values, sizeof(player->colors));
	player->gear_mode = profile->gear_mode;
	player->control_type = profile->control_type;
}

static int
asset_name_matches(const char *recorded, const char *loaded)
{
	size_t length;

	/* Asset names must be nonempty and terminated within the field. */
	for (length = 0; length < MH_DEMO_NAME_BYTES && recorded[length] != 0;
		length++) {
	}

	return length != 0 && length < MH_DEMO_NAME_BYTES &&
		mh_ascii_equal_n(recorded, loaded, length + 1);
}

enum mh_error_code
mh_network_capture_header(const struct mh_net_race *race,
	const struct mh_net_session *session, struct mh_demo *header,
	struct mh_error *error_out)
{
	struct mh_demo next;
	struct mh_demo_player *player;
	const struct mh_net_profile *profile;
	unsigned char header_bytes[MH_DEMO_MAX_HEADER_BYTES];
	unsigned int entry, slot;
	enum mh_error_code status;

	if (!valid_network_race(race) || session == NULL || header == NULL ||
		race->started ||
		(session->phase != MH_NET_WAITING &&
			session->phase != MH_NET_RACING) ||
		session->roster.player_count < 1 ||
		session->roster.player_count > MH_DEMO_MAX_PLAYERS ||
		session->player != race->layout.player[0] ||
		session->roster.laps != race->field.cars[0].track->laps ||
		race->field.active_mask != (1U << race->field.count) - 1)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"recording requires a fresh network race");

	memset(&next, 0, sizeof(next));
	next.version = MH_DEMO_VERSION_30;

	if (race->field.cars[0].frame.fixed_rate) {
		next.version = MH_DEMO_VERSION_40;
		next.timing_version = MH_SIM_TIMING_VERSION;
		next.simulation_rate = race->field.cars[0].frame.fixed_rate;
	}

	next.player_count = race->field.count;
	next.local_player = session->player;
	next.laps = session->roster.laps;
	next.flags = race->field.tuning.catchup ? 1 : 0;
	copy_identity_bytes(
		next.track, session->roster.track, MH_DEMO_NAME_BYTES);

	if (!asset_name_matches(next.track, race->field.cars[0].track->name))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"network recording track changed");

	for (slot = 0; slot < MH_DEMO_MAX_PLAYERS; slot++) {
		player = &next.players[slot];
		profile = &session->roster.players[slot];
		player->active =
			race->layout.entry[slot] != MH_NET_WIRE_PLAYERS;

		if (player->active !=
			(unsigned long)(slot < session->roster.player_count &&
				(profile->flags & MH_NET_FLAG_RACING) != 0))
			return mh_error_set(error_out, MH_ERROR_STATE,
				"network recording roster changed");

		copy_player_identity(player, profile);
	}

	for (entry = 0; entry < race->field.count; entry++) {
		slot = race->layout.player[entry];

		if (race->field.cars[entry].ticks != 0 ||
			race->field.cars[entry].start_ticks != 0)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"network recording must begin before loading wait");

		if (!asset_name_matches(next.players[slot].car,
			race->field.cars[entry].car->name))
			return mh_error_set(error_out, MH_ERROR_STATE,
				"network recording car changed");
	}

	status = mh_demo_header_write(
		&next, header_bytes, sizeof(header_bytes), error_out);

	if (status == MH_ERROR_NONE)
		*header = next;

	return status;
}

enum mh_error_code
mh_network_capture_sample(const struct mh_net_race *race,
	const struct mh_demo *header, struct mh_demo_record_sample *sample,
	struct mh_error *error_out)
{
	struct mh_demo_record_sample next;
	struct mh_net_vehicle vehicle;
	unsigned int entry, slot;
	enum mh_error_code status;

	if (!valid_network_race(race) || header == NULL || sample == NULL ||
		header->simulation_rate !=
			race->field.cars[0].frame.fixed_rate ||
		header->player_count != race->field.count ||
		header->local_player != race->layout.player[0] ||
		header->laps != race->field.cars[0].track->laps ||
		header->flags != (race->field.tuning.catchup ? 1UL : 0UL))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"network recording roster/settings changed");

	for (slot = 0; slot < MH_DEMO_MAX_PLAYERS; slot++)
		if ((header->players[slot].active != 0) !=
			(race->layout.entry[slot] != MH_NET_WIRE_PLAYERS))
			return mh_error_set(error_out, MH_ERROR_STATE,
				"network recording initial slots changed");

	memset(&next, 0, sizeof(next));

	for (entry = 0; entry < race->field.count; entry++) {
		slot = race->layout.player[entry];

		if (race->field.active_mask & (1U << entry))
			next.active_mask |= 1U << slot;

		if (race->field.unavailable_mask & (1U << entry))
			next.unavailable_mask |= 1U << slot;

		status = entry == 0
			? mh_net_drive_capture(&race->field.cars[entry], NULL,
				&vehicle, error_out)
			: mh_net_drive_capture_remote(&race->field.cars[entry],
				&race->samples[entry].replica.vehicle,
				&vehicle, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_net_vehicle_encode(&vehicle,
				next.bodies[slot], MH_DEMO_VEHICLE_BYTES,
				error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*sample = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_network_capture_crossings(const struct mh_net_race *race,
	struct mh_demo_player players[MH_DEMO_MAX_PLAYERS],
	struct mh_error *error_out)
{
	struct mh_demo_player next[MH_DEMO_MAX_PLAYERS];
	unsigned int entry;

	if (!valid_network_race(race) || players == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network recording checkpoint source");

	memset(next, 0, sizeof(next));

	for (entry = 0; entry < race->field.count; entry++)
		memcpy(next[race->layout.player[entry]].crossings,
			race->field.cars[entry].race.splits,
			sizeof(next[entry].crossings));

	memcpy(players, next, sizeof(next));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_network_capture_advance(struct mh_net_race *race,
	struct mh_net_session *session, const struct mh_engine_input *controls,
	long double elapsed, double now_ms, unsigned int viewed_entry,
	const struct mh_demo *header, struct mh_demo_capture_frame *capture,
	unsigned int *steps, struct mh_error *error_out)
{
	struct mh_net_race next;
	struct mh_net_session next_session;
	struct mh_demo_capture_frame frame;
	unsigned int step_count;
	enum mh_error_code status;

	if (!valid_network_race(race) || session == NULL || header == NULL ||
		capture == NULL || steps == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recorded network frame");

	next = *race;
	next_session = *session;
	memset(&frame, 0, sizeof(frame));
	status = mh_net_race_frame_physics(&next, &next_session, controls,
		elapsed, now_ms, viewed_entry, &step_count, error_out);

	if (status == MH_ERROR_NONE && !next.ended) {
		status = mh_frame_media_time(next.field.cars[0].frame.seconds,
			next.field.cars[0].frame.intro_ms, &frame.ready,
			&frame.clock_ms, error_out);

		if (status == MH_ERROR_NONE && frame.ready)
			status = mh_network_capture_sample(
				&next, header, &frame.sample, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_net_race_frame_finish(
				&next, &next_session, error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	*race = next;
	*session = next_session;
	*capture = frame;
	*steps = step_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
