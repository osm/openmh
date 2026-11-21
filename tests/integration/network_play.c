#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../support/net_virtual_io.h"
#include "client/diagnostics/asset_audio.h"
#include "client/diagnostics/asset_config_load.h"
#include "client/diagnostics/runtime_network.h"
#include "client/diagnostics/runtime_network_setup.h"
#include "client/diagnostics/runtime_race.h"
#include "client/race/race.h"
#include "client/session/network_race.h"
#include "engine/audio/race_playback.h"
#include "engine/audio/race_sound_assets.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/render/scene_matrices.h"
#include "engine/ui/lobby_session.h"

/* Retain a complete network issue diagnostic across owner cleanup. */
#define FAILURE_MESSAGE_BYTES 512

static struct mh_network *networks[8];
static struct mh_race_context contexts[8];
static struct mh_net_race races[8];
static struct mh_network_race driving[8];
static unsigned long frames, samples;

static enum mh_error_code
chat_burst(unsigned int sender, struct mh_error *error_out)
{
	static const char format[] = "NetDriver%u: Burst line %02u";
	struct mh_engine_input controls;
	struct mh_hud_state hud;
	struct mh_drive_frame sampled;
	const struct mh_messages *messages;
	unsigned long revisions[8], previous, milliseconds;
	unsigned int i, j, frame, steps, ready;
	int done;
	enum mh_error_code status;
	char line[64];

	for (i = 0; i < 8; i++)
		revisions[i] = driving[i].messages.revision;

	for (j = 0; j < 24; j++) {
		sprintf(line, format, sender, j);
		milliseconds = (unsigned long)mh_drive_clock_ms(
			&races[sender].field.cars[0]);
		status = driving[sender].race_hooks.send_chat(&driving[sender],
			line, 0x112233UL + sender, milliseconds, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (driving[sender].messages.rows[0].milliseconds !=
			milliseconds)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"local chat did not use supplied game time");
	}

	memset(&controls, 0, sizeof(controls));

	for (frame = 0; frame < 500; frame++) {
		mh_test_net_advance(10);
		ready = 0;

		for (i = 0; i < 8; i++) {
			status = mh_drive_frame_sample(&races[i].field.cars[0],
				.0005L, &sampled, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			milliseconds =
				(unsigned long)((long double)sampled.seconds *
					1000.0L);
			previous = driving[i].messages.revision;
			status = driving[i].race_hooks.advance(&driving[i],
				&controls, .0005L, MH_FRAME_RUN, &steps, &done,
				error_out);

			if (status != MH_ERROR_NONE || done)
				return status != MH_ERROR_NONE
					? status
					: mh_error_set(error_out,
						MH_ERROR_FAILED,
						"chat ended race");

			if (races[i].field.cars[0].frame.seconds !=
				sampled.seconds)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"chat double-advanced the game clock");

			for (j = 0; j < driving[i].messages.revision - previous;
				j++)
				if (driving[i].messages.rows[j].milliseconds !=
					milliseconds)
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"received chat used a different frame clock");

			if (driving[i].messages.revision == revisions[i] + 24)
				ready++;
		}

		if (ready == 8 && frame >= 50)
			break;
	}

	if (frame == 500)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"chat burst lost or duplicated messages");

	for (i = 0; i < 8; i++) {
		mh_hud_state(&races[i].field, 0, &hud, NULL);
		driving[i].race_hooks.hud(&driving[i], &hud);
		messages = hud.messages;

		if (messages != &driving[i].messages)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"network chat absent from HUD history");

		milliseconds = (unsigned long)mh_drive_clock_ms(
			&races[i].field.cars[0]);

		if (hud.message_ms != milliseconds ||
			hud.chat_ms != milliseconds)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"network HUD uses a different game clock");

		for (j = 0; j < 24; j++) {
			sprintf(line, format, sender, 23 - j);

			if (strcmp(messages->rows[j].text, line) ||
				messages->rows[j].color != 0x112233UL + sender)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"network chat order/color differs");
		}
	}

	/*
	 * Local notices share this history but must never become network chat.
	 */
	status = driving[sender].race_hooks.message(&driving[sender],
		"Saved picture: ", "motor0.tga", 0xe06820UL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (strcmp(driving[sender].messages.rows[0].text,
		"Saved picture: motor0.tga"))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"network screenshot notice missing");

	strcpy((char *)driving[sender].race_hooks.chat->history.rows[0].text,
		"Unsent draft");
	driving[sender].race_hooks.chat->active = 1;
	mh_hud_state(&races[sender].field, 0, &hud, NULL);
	driving[sender].race_hooks.hud(&driving[sender], &hud);

	if (hud.chat_text == NULL || strcmp(hud.chat_text, "Unsent draft"))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"chat editor absent from network HUD");

	driving[sender].race_hooks.chat->active = 0;

	if (driving[sender].race_hooks.chat !=
		mh_network_chat_state(networks[sender]))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"draft history detached from connection");

	printf("peer %u: 24-line chat burst delivered once to all eight HUDs with current game stamps under native datagram loss\n",
		sender);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
info_overlay(unsigned int peer, struct mh_error *error_out)
{
	static struct mh_ui_list info_list;
	struct mh_hud_state hud;
	const struct mh_race_info_row *row;
	const struct mh_net_session *session;
	const struct mh_net_profile *profile;
	const struct mh_field *field;
	enum mh_error_code status;
	unsigned int i, slot, seen;
	unsigned long position;

	field = &races[peer].field;
	session = mh_network_session(networks[peer]);
	mh_hud_state(field, 0, &hud, NULL);
	hud.show_info = 1;
	status = mh_race_info_state(field, contexts[peer].results.profiles,
		NULL, &hud.info, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Portrait/profile lookup may sanitize or substitute an identity. The
	 * Info table must take the original roster bytes and colors instead.
	 */
	for (i = 0; i < hud.info.count; i++) {
		strcpy(hud.info.rows[i].name, "Portrait fallback");
		hud.info.rows[i].team[0] = 0;
		hud.info.rows[i].color = 0;
	}

	driving[peer].race_hooks.hud(&driving[peer], &hud);
	seen = 0;

	for (i = 0; i < hud.info.count; i++) {
		row = &hud.info.rows[i];
		slot = races[peer].layout.player[row->entry];
		profile = &session->roster.players[slot];
		seen |= 1U << row->entry;

		if (!session->host && session->replication.status_revision != 0)
			position = session->replication.status[slot].value_a;
		else
			position = field->standings.cars[row->entry].position;

		if (row->position != position ||
			row->ping !=
				session->replication.status[slot].ping_ms ||
			row->control_type != profile->control_type ||
			row->automatic != profile->gear_mode ||
			strcmp(row->name, profile->name) ||
			strcmp(row->team, profile->team) ||
			row->color != profile->values[0])
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"network Info metadata/slot mapping differs");
	}

	if (seen != field->active_mask)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"network Info includes a departed player");

	if (!mh_hud_draw(&contexts[peer].hud, &hud, 640, 480, &info_list) ||
		info_list.count == 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"network Info composition failed");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
pump(struct mh_error *error_out)
{
	unsigned int i;
	struct mh_network_issue issue;
	enum mh_error_code status;

	for (i = 0; i < 8; i++)
		if (networks[i] != NULL) {
			status = mh_network_drain(
				networks[i], 64, &issue, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (issue.reason.code != MH_ERROR_NONE)
				return mh_error_forward(
					error_out, &issue.reason);
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
wait_phase(enum mh_net_session_phase phase, struct mh_error *error_out)
{
	unsigned int frame, i, count, expected;
	unsigned int steps;
	struct mh_engine_input controls;
	int done;
	enum mh_error_code status;

	memset(&controls, 0, sizeof(controls));

	for (frame = 0; frame < 1000; frame++) {
		mh_test_net_advance(10);
		status = pump(error_out);

		if (status != MH_ERROR_NONE)
			return status;

		count = expected = 0;

		for (i = 0; i < 8; i++)
			if (networks[i] != NULL) {
				expected++;

				if (phase == MH_NET_RACING) {
					status = driving[i].race_hooks.advance(
						&driving[i], &controls, .01,
						MH_FRAME_RUN, &steps, &done,
						error_out);

					if (status != MH_ERROR_NONE || done)
						return status != MH_ERROR_NONE
							? status
							: mh_error_set(
								error_out,
								MH_ERROR_FAILED,
								"race ended during loading barrier");
				}

				if (mh_network_session(networks[i])->phase ==
					phase)
					count++;
			}

		/*
		 * Lobby phase precedes return acknowledgements and admission
		 * cleanup. A duplicated native HELLO can leave an unadmitted
		 * connection until its original timeout expires.
		 */
		if (count == expected &&
			(phase != MH_NET_LOBBY ||
				mh_network_race_ready(
					mh_network_session(networks[0]),
					expected)))
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_error_set(error_out, MH_ERROR_FAILED,
		"offline peers did not reach expected phase");
}

/*
 * Deliberately injected host finish observations exercise orchestration and
 * reliable delivery independently of driver skill/AI. The earlier section
 * exercises real vehicle updates; these fixtures do not claim physical laps.
 */
static enum mh_error_code
finish_round(int all, unsigned long round_number, struct mh_error *error_out)
{
	static const unsigned int points[8] = {12, 10, 8, 6, 4, 3, 2, 1};
	struct mh_net_session *host, *session;
	struct mh_engine_input controls;
	struct mh_hud_state hud;
	unsigned long before[8], expected[8];
	unsigned int i, j, frame, steps, entry, slot, ended, warned, expired;
	double anchor, elapsed;
	int done;
	enum mh_error_code status;

	host = mh_network_session(networks[0]);

	for (i = 0; i < 8; i++) {
		before[i] = host->roster.players[i].values[5];
		expected[i] = before[i];
	}

	for (i = 0; i < races[0].field.count; i++) {
		races[0].field.cars[i].start_ticks = 1680;

		if (all || i == 2) {
			races[0].field.cars[i].race.finished = 1;
			races[0].field.cars[i].race.now_ms =
				60000 + (7 - i) * 1000;
		}
	}

	memset(&controls, 0, sizeof(controls));
	anchor = -1;
	warned = expired = 0;

	for (frame = 0; frame < 400; frame++) {
		mh_test_net_advance(100);
		ended = 0;

		for (i = 0; i < 7; i++) {
			elapsed = .1;
			status = driving[i].race_hooks.advance(&driving[i],
				&controls, elapsed, MH_FRAME_RUN, &steps, &done,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (done)
				ended++;

			session = mh_network_session(networks[i]);

			if (session->phase == MH_NET_RACING &&
				session->notice_revision != 0) {
				mh_hud_state(&races[i].field, 0, &hud, NULL);
				driving[i].race_hooks.hud(&driving[i], &hud);

				if (hud.message == NULL ||
					strcmp(hud.message,
						session->notice.text) != 0)
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"finish warning missing from HUD");

				if (strncmp(session->notice.text,
					"Time left:", 10) == 0)
					warned |= 1U << i;

				if (strcmp(session->notice.text,
					"Did not finish!") == 0)
					expired |= 1U << i;
			}
		}

		if (frame == 0) {
			anchor = (races[0].finish.anchor_ms +
					(all ? 25000UL : 0UL)) &
				MH_U32_MAX;

			if (anchor <= 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"host did not start finish timer");

			for (j = 0; j < races[0].field.count; j++) {
				slot = races[0].layout.player[j];
				entry = races[0].field.standings.cars[j]
						.position;

				if (entry < 1 || entry > 7)
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"invalid completed position");

				expected[slot] =
					before[slot] + points[entry - 1];
			}
		}

		if (host->phase == MH_NET_LOBBY &&
			mh_drive_clock_ms(&races[0].field.cars[0]) <=
				anchor + (all ? 10000 : 35000))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"host ended race before the original strict deadline");

		if (ended == 7)
			break;
	}

	if (frame == 400 || !races[0].ended || !driving[0].returned_to_lobby)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"finish timer did not return all peers");

	/*
	 * Unfinished cars keep moving while the game-clock deadline runs. Award
	 * their final classification, not their rank at the first finish.
	 */
	for (j = 0; j < races[0].field.count; j++) {
		slot = races[0].layout.player[j];
		entry = races[0].field.standings.cars[j].position;

		if (entry < 1 || entry > 7)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"invalid final completed position");

		expected[slot] = before[slot] + points[entry - 1];
	}

	status = wait_phase(MH_NET_LOBBY, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * A phase can arrive one reliable packet before the final scored
	 * roster.
	 */
	for (frame = 0; frame < 50; frame++) {
		mh_test_net_advance(20);
		status = pump(error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 0; i < 7; i++) {
		session = mh_network_session(networks[i]);

		if (memcmp(&session->roster, &host->roster,
			sizeof(host->roster)) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"round scores did not converge");

		for (j = 0; j < races[0].field.count; j++) {
			slot = races[0].layout.player[j];

			if (session->roster.players[slot].values[4] !=
					round_number ||
				session->roster.players[slot].values[5] !=
					expected[slot])
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"round count/points differ from host classification");
		}

		if (!all && races[0].layout.entry[session->player] != 2 &&
			(!(warned & (1U << i)) || !(expired & (1U << i))))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"targeted countdown/DNF notice was lost");
	}

	printf("round %lu: %s, original deadline and all seven cumulative scoreboards passed\n",
		round_number,
		all ? "all finished" : "unfinished racers timed out");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
shade_unavailable_cars(unsigned int peer, const float *models,
	const float *view, struct mh_error *error_out)
{
	unsigned int entry, part;
	enum mh_error_code status;

	status = MH_ERROR_NONE;

	for (entry = 0;
		entry < races[peer].field.count && status == MH_ERROR_NONE;
		entry++) {
		if (!(races[peer].field.unavailable_mask & (1U << entry)))
			continue;

		for (part = 0; part < 5 && status == MH_ERROR_NONE; part++)
			status = mh_race_scene_shade_unavailable(
				&contexts[peer].scene, &races[peer].field,
				1 + entry * 5 + part, models, view, error_out);
	}

	return status;
}

static enum mh_error_code
elimination_round(const char *root, int all, int with_eliminated,
	unsigned long round_number, struct mh_error *error_out)
{
	static const unsigned int points[8] = {12, 10, 8, 6, 4, 3, 2, 1};
	static struct mh_race_sound sounds[7];
	struct mh_race_sound_assets sound_assets;
	struct mh_net_session *host, *session;
	struct mh_engine_input controls;
	struct mh_hud_state hud;
	unsigned long expected[8], before[8];
	unsigned int i, j, frame, steps, done_count, slot, rank, winner, seen,
		status_seen;
	unsigned int shaded;
	double anchor;
	int done;
	enum mh_error_code status;
	float pcm[128];
	float models[MH_FIELD_MESHES * 16], matrix[16], view[16];

	host = mh_network_session(networks[0]);
	host->config.mode = 1;
	status = mh_net_session_begin_race(
		host, mh_network_now(networks[0]), error_out);

	if (status == MH_ERROR_NONE)
		status = wait_phase(MH_NET_LOADING, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 7; i++) {
		mh_race_context_free(&contexts[i]);
		session = mh_network_session(networks[i]);
		status = mh_network_setup_load(
			root, session, 0, &contexts[i], &races[i], error_out);

		if (status == MH_ERROR_NONE)
			status = mh_network_setup_ui(root, session, &races[i],
				&contexts[i], error_out);

		if (status == MH_ERROR_NONE)
			status = mh_network_race_init(networks[i], &races[i],
				&contexts[i], &driving[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (session->roster.mode != 1 ||
			races[i].elimination.eliminated ||
			races[i].field.race_notice_revision)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"elimination reload retained old mode or event state");
	}

	status = wait_phase(MH_NET_RACING, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 8; i++) {
		before[i] = host->roster.players[i].values[5];
		expected[i] = before[i];
	}

	winner = races[0].layout.player[3];

	for (i = 0; i < 7; i++) {
		unsigned int entry;
		struct mh_drive *drive;

		races[i].started = 1;

		for (entry = 0; entry < races[i].field.count; entry++) {
			drive = &races[i].field.cars[entry];
			drive->frame.active = 1;
			drive->frame.seconds = 20;
			drive->frame.intro_ms = 1;
			drive->frame.time.now_ms = 20000;
			drive->frame.time.go_ms = 10000;
			drive->seconds = 19.999;
		}

		races[0].field.cars[i].race.now_ms =
			all ? 60000 + (7 - i) * 1000 : 10000;
		races[0].field.cars[i].race.finished = all;
		races[0].field.standings.cars[i].position = (i + 7 - 3) % 7 + 1;
	}

	/* Inject one eliminated entrant beside six completed observations. */
	if (with_eliminated) {
		races[0].elimination.eliminated = 1U
			<< races[0].layout.player[1];
		races[0].field.cars[1].race.finished = 0;
		races[0].field.cars[1].race_frozen = 1;
	}

	if (!all) {
		struct mh_net_elimination_sample observations[8];
		unsigned int mask;

		memset(observations, 0, sizeof(observations));
		mask = 0;

		for (i = 0; i < 7; i++) {
			slot = races[0].layout.player[i];
			mask |= 1U << slot;
			observations[slot].lap = 1;
			observations[slot].lap_limit = host->roster.laps;
			observations[slot].position = (i + 7 - 3) % 7 + 1;
			observations[slot].elapsed_ms = 10000;
			observations[slot].split = i == 3 ? 2 : 0;
		}

		/*
		 * Inject a prior controller observation. Actual frame-end
		 * checkpoint production is exercised by test-net-race; this
		 * tests native reliable delivery.
		 */
		status = mh_net_elimination_step(&races[0].elimination,
			observations, host->roster.player_count, mask, 20000,
			&races[0].elimination_state, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status =
		mh_race_sound_assets_load(root, NULL, &sound_assets, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 7 && status == MH_ERROR_NONE; i++)
		status = mh_race_sound_init(48000, .35, &sounds[i], error_out);

	memset(&controls, 0, sizeof(controls));
	anchor = -1;
	seen = status_seen = shaded = 0;

	for (frame = 0; frame < 200 && status == MH_ERROR_NONE; frame++) {
		mh_test_net_advance(100);
		done_count = 0;

		for (i = 0; i < 7 && status == MH_ERROR_NONE; i++) {
			status = driving[i].race_hooks.advance(&driving[i],
				&controls, .1, MH_FRAME_RUN, &steps, &done,
				error_out);

			if (status != MH_ERROR_NONE)
				break;

			if (done)
				done_count++;

			session = mh_network_session(networks[i]);

			if (session->phase != MH_NET_RACING)
				continue;

			if (!(shaded & (1U << i)) &&
				races[i].field.unavailable_mask != 0) {
				mh_field_matrices(&races[i].field, 1.5, models,
					matrix, view);

				status = shade_unavailable_cars(
					i, models, view, error_out);

				if (status != MH_ERROR_NONE)
					break;

				shaded |= 1U << i;
			}

			if (i != 0)
				for (j = 0; j < 7; j++)
					if (races[i].field.cars[j].race_frozen)
						status = mh_error_set(error_out,
							MH_ERROR_FAILED,
							"client incorrectly froze progress from host elimination");

			if (!all && session->notice_revision &&
				strstr(session->notice.text, "Time left:") ==
					NULL) {
				if (session->notice.text[0] == 0)
					continue;

				mh_hud_state(&races[i].field, 0, &hud, NULL);
				hud.finished = 1;
				driving[i].race_hooks.hud(&driving[i], &hud);

				if (hud.message == NULL ||
					strcmp(hud.message,
						session->notice.text) != 0)
					status = mh_error_set(error_out,
						MH_ERROR_FAILED,
						"elimination notice disappeared behind finish HUD");

				if (session->player == winner) {
					if (strcmp(session->notice.text,
						"You are the winner!") !=
							0 ||
						races[i].field.race_notice !=
							MH_RACE_NOTICE_WIN)
						status = mh_error_set(error_out,
							MH_ERROR_FAILED,
							"winner notice/cue missing");
				} else if (
					strncmp(session->notice.text,
						"You have been eliminated by ",
						28) != 0 ||
					races[i].field.race_notice !=
						MH_RACE_NOTICE_OUT)
					status = mh_error_set(error_out,
						MH_ERROR_FAILED,
						"elimination notice/cue missing");

				if (status != MH_ERROR_NONE)
					break;

				status = mh_race_sound_update(&sounds[i],
					&sound_assets.bank, &races[i].field, 0,
					0, error_out);

				if (status != MH_ERROR_NONE)
					break;

				if (!(seen & (1U << i))) {
					const struct mh_mixer_voice *voice,
						*echo;

					j = session->player == winner ? 18 : 29;
					voice = &sounds[i].mixer.voices[j];
					echo = &sounds[i].mixer.voices
							[session->player == winner
									? 28
									: 30];

					if (!voice->active || !echo->active)
						status = mh_error_set(error_out,
							MH_ERROR_FAILED,
							"received elimination sound did not start both original copies");

					if (session->player != winner &&
						voice->pitch !=
							(double)(float).9)
						status = mh_error_set(error_out,
							MH_ERROR_FAILED,
							"elimination sample lost original pitch");
				}

				seen |= 1U << i;

				if (status == MH_ERROR_NONE)
					status = mh_mixer_render(
						&sounds[i].mixer, pcm, 64, 0,
						error_out);

				if (status != MH_ERROR_NONE)
					break;

				for (j = 0; j < 128; j++)
					if (!(pcm[j] >= -1 && pcm[j] <= 1))
						status = mh_error_set(error_out,
							MH_ERROR_FAILED,
							"invalid elimination PCM");

				if (session->player != winner &&
					(session->replication
							.status[session->player]
							.flags &
						8))
					status_seen |= 1U << i;
			}
		}

		if (frame == 0)
			for (j = 0; j < 7; j++) {
				slot = races[0].layout.player[j];
				rank = races[0].field.standings.cars[j]
					.position;

				if (rank < 1 || rank > 7) {
					status = mh_error_set(error_out,
						MH_ERROR_FAILED,
						"invalid elimination award rank");
					break;
				}

				expected[slot] =
					before[slot] + points[rank - 1];
			}

		if (anchor < 0 &&
			(all ? races[0].finish.anchor_ms != 0
			: races[0].elimination.anchor_ms != 0))
			anchor = all ? (races[0].finish.anchor_ms + 25000UL) &
					MH_U32_MAX
				: races[0].elimination.anchor_ms;

		if (host->phase == MH_NET_LOBBY &&
			(anchor < 0 ||
				mh_drive_clock_ms(&races[0].field.cars[0]) <=
					anchor + (all ? 10000 : 5000)))
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"elimination round exited before original deadline");

		if (done_count == 7)
			break;
	}

	mh_race_sound_assets_free(&sound_assets);

	if (status != MH_ERROR_NONE)
		return status;

	if (frame == 200 || !races[0].ended || (!all && seen != 127))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"elimination round did not reach every frontend");

	if (!all && shaded != 127)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"unavailable lighting did not reach every frontend");

	if (!all)
		for (i = 0; i < 7; i++)
			if (mh_network_session(networks[i])->player != winner &&
				!(status_seen & (1U << i)))
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"eliminated status did not reach peer");

	for (j = 0; j < 7; j++) {
		slot = races[0].layout.player[j];
		rank = races[0].field.standings.cars[j].position;

		if (rank < 1 || rank > 7)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"invalid final elimination rank");

		expected[slot] = before[slot] + points[rank - 1];
	}

	status = wait_phase(MH_NET_LOBBY, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (frame = 0; frame < 50; frame++) {
		mh_test_net_advance(20);
		status = pump(error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 0; i < 7; i++) {
		session = mh_network_session(networks[i]);

		if (memcmp(&session->roster, &host->roster,
			sizeof(host->roster)) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"elimination scores did not converge");

		for (j = 0; j < 7; j++) {
			slot = races[0].layout.player[j];

			if (session->roster.players[slot].values[4] !=
					round_number ||
				session->roster.players[slot].values[5] !=
					expected[slot])
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"elimination counters/points differ from classification");
		}

		if (!all &&
			(session->chat_count < 7 ||
				strstr(session->chat[session->chat_count - 1]
						.text,
					" is the winner!") == NULL ||
				memcmp(session->chat, host->chat,
					sizeof(host->chat)) != 0))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"elimination broadcasts did not arrive in order");
	}

	if (with_eliminated &&
		(races[0].field.cars[1].race.finished ||
			!races[0].finish.all_finished ||
			races[0].elimination.anchor_ms))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"eliminated nonfinisher did not use surviving finishers' deadline");

	printf("round %lu: elimination %s; seven peers, %s passed\n",
		round_number,
		with_eliminated
			? "six finishers and one eliminated"
			: (all ? "all finished" : "six simultaneous cutoffs"),
		all ? "shared deadline and scores"
		: "original notices/audio/status/deadline/scores");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
reload_peer(const char *root, unsigned int i, struct mh_error *error_out)
{
	struct mh_net_session *session;
	enum mh_error_code status;

	mh_race_context_free(&contexts[i]);
	session = mh_network_session(networks[i]);
	status = mh_network_setup_load(
		root, session, 0, &contexts[i], &races[i], error_out);

	if (status == MH_ERROR_NONE)
		status = mh_network_setup_ui(
			root, session, &races[i], &contexts[i], error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_network_race_init(
		networks[i], &races[i], &contexts[i], &driving[i], error_out);
}

static enum mh_error_code
mixed_round(const char *root, struct mh_error *error_out)
{
	static const unsigned int points[8] = {12, 10, 8, 6, 4, 3, 2, 1};
	static const char waiting_chat[] = "NetDriver1: waiting-room message";
	static const char racing_chat[] = "NetDriver2: racing-room message";
	struct mh_net_session_config config;
	struct mh_net_session *host, *session;
	struct mh_engine_input controls;
	struct mh_lobby_roster lobby;
	unsigned long before[8], races_before[8], expected[8], chat_revision[8];
	unsigned int i, j, frame, steps, late, returned, entry, rank;
	int done;
	enum mh_error_code status;

	host = mh_network_session(networks[0]);
	host->config.mode = 0;
	status = mh_net_session_begin_race(
		host, mh_network_now(networks[0]), error_out);

	if (status == MH_ERROR_NONE)
		status = wait_phase(MH_NET_LOADING, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 7; i++) {
		status = reload_peer(root, i, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = wait_phase(MH_NET_RACING, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	config = host->config;
	strcpy(config.profile.name, "LateDriver");
	config.profile.values[4] = 987;
	config.profile.values[5] = 654;
	status = mh_network_open(0, NULL, "127.0.0.1",
		mh_network_port(networks[0]), &config, &networks[7], error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&controls, 0, sizeof(controls));
	controls.throttle = .5;

	for (frame = 0; frame < 1000; frame++) {
		mh_test_net_advance(17);
		status = pump(error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < 7; i++) {
			status = driving[i].race_hooks.advance(&driving[i],
				&controls, 1.0 / 60, MH_FRAME_RUN, &steps,
				&done, error_out);

			if (status != MH_ERROR_NONE || done)
				return status != MH_ERROR_NONE
					? status
					: mh_error_set(error_out,
						MH_ERROR_FAILED,
						"late admission ended an active race");
		}

		if (mh_network_session(networks[7])->phase == MH_NET_LOBBY)
			break;
	}

	if (frame == 1000)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"late player did not join the lobby");

	late = mh_network_session(networks[7])->player;
	returned = mh_network_session(networks[1])->player;

	if (host->roster.players[late].flags != 2 ||
		host->roster.players[late].values[4] != 0 ||
		host->roster.players[late].values[5] != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"late admission retained supplied scores");

	for (i = 0; i < 7; i++)
		if (races[i].field.count != 7 ||
			races[i].layout.entry[late] != MH_NET_WIRE_PLAYERS)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"late admission changed the frozen field");

	status = driving[1].race_hooks.restart(&driving[1], error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (frame = 0; frame < 200; frame++) {
		mh_test_net_advance(17);
		status = pump(error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < 7; i++) {
			status = driving[i].race_hooks.advance(&driving[i],
				&controls, 1.0 / 60, MH_FRAME_RUN, &steps,
				&done, error_out);

			if (status != MH_ERROR_NONE || (i != 1 && done))
				return status != MH_ERROR_NONE
					? status
					: mh_error_set(error_out,
						MH_ERROR_FAILED,
						"individual return ended another race");
		}
	}

	if (!driving[1].returned_to_lobby || host->phase != MH_NET_RACING ||
		mh_network_session(networks[1])->phase != MH_NET_LOBBY ||
		host->lobby.slots[returned].phase != MH_NET_IN_LOBBY)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"individual frontend return failed");

	for (i = 1; i < 8; i += 6) {
		session = mh_network_session(networks[i]);
		status = mh_lobby_session_roster(
			session, "Motorhead", 0, &lobby, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (session->replication.configured ||
			session->replication.status_revision == 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"waiting player did not receive independent lobby status");

		for (j = 0; j < 8; j++)
			if (host->roster.players[j].flags & 1) {
				if (!lobby.players[j].racing ||
					lobby.players[j].position < 1 ||
					lobby.players[j].position > 6 ||
					lobby.players[j].lap !=
						host->replication.status[j]
							.value_b)
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"waiting lobby roster did not follow live race standings");
			}
	}

	for (i = 0; i < 7; i++)
		if (i != 1) {
			entry = races[i].layout.entry[returned];

			if (races[i].field.active_mask & (1U << entry))
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"returned car remains active");

			if (!(contexts[i].results.left_mask & (1U << entry)))
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"returned car missing result annotation");
		}

	for (i = 0; i < 8; i++)
		chat_revision[i] =
			mh_network_session(networks[i])->chat_revision;

	status = mh_net_session_chat(
		mh_network_session(networks[1]), waiting_chat, 7, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_net_session_chat(mh_network_session(networks[2]),
			racing_chat, 8, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (frame = 0; frame < 100; frame++) {
		mh_test_net_advance(20);
		status = pump(error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 0; i < 8; i++) {
		session = mh_network_session(networks[i]);

		if (session->chat_revision != chat_revision[i] + 1 ||
			session->chat_count == 0 ||
			strcmp(session->chat[session->chat_count - 1].text,
				i == 1 || i == 7 ? waiting_chat
						: racing_chat) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"mixed chat crossed lobby/race audiences");
	}

	for (i = 0; i < 8; i++) {
		before[i] = expected[i] = host->roster.players[i].values[5];
		races_before[i] = host->roster.players[i].values[4];
	}

	for (i = 0; i < races[0].field.count; i++)
		if (races[0].field.active_mask & (1U << i)) {
			races[0].field.cars[i].race.finished = 1;
			races[0].field.cars[i].race.now_ms = 60000 + i * 1000;
		}

	for (frame = 0; frame < 200; frame++) {
		mh_test_net_advance(100);
		status = pump(error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < 7; i++)
			if (i != 1) {
				status = driving[i].race_hooks.advance(
					&driving[i], &controls, .1,
					MH_FRAME_RUN, &steps, &done, error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}

		if (frame == 0)
			for (i = 0; i < races[0].field.count; i++)
				if (races[0].field.active_mask & (1U << i)) {
					j = races[0].layout.player[i];
					rank = races[0].field.standings.cars[i]
						.position;

					if (rank < 1 || rank > 6)
						return mh_error_set(error_out,
							MH_ERROR_FAILED,
							"mixed field classification is not compact");

					expected[j] =
						before[j] + points[rank - 1];
					races_before[j]++;
				}

		if (host->phase == MH_NET_LOBBY)
			break;
	}

	if (frame == 200)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"mixed race did not complete");

	status = wait_phase(MH_NET_LOBBY, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (frame = 0; frame < 100; frame++) {
		mh_test_net_advance(20);
		status = pump(error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 0; i < 8; i++) {
		session = mh_network_session(networks[i]);

		if (memcmp(&session->roster, &host->roster,
			sizeof(session->roster)) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"mixed round rosters did not converge");

		for (j = 0; j < 8; j++)
			if (session->roster.players[j].values[4] !=
					races_before[j] ||
				session->roster.players[j].values[5] !=
					expected[j])
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"lobby member incorrectly scored");
	}

	status = mh_net_session_begin_race(
		host, mh_network_now(networks[0]), error_out);

	if (status == MH_ERROR_NONE)
		status = wait_phase(MH_NET_LOADING, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 8; i++) {
		status = reload_peer(root, i, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (races[i].field.count != 8 ||
			contexts[i].results.left_mask != 0 ||
			races[i].field.cars[0].race.now_ms != 0 ||
			races[i].field.cars[0].start_ticks != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"mixed next race did not rebuild all eight entries");

		entry = races[i].layout.entry[late];

		if (entry >= 8 ||
			strcmp(contexts[i].results.profiles[entry].name,
				"LateDriver") != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"late driver identity missing from next field");
	}

	status = wait_phase(MH_NET_RACING, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (frame = 0; frame < 180; frame++) {
		mh_test_net_advance(17);

		for (i = 0; i < 8; i++) {
			status = driving[i].race_hooks.advance(&driving[i],
				&controls, 1.0 / 60, MH_FRAME_RUN, &steps,
				&done, error_out);

			if (status != MH_ERROR_NONE || done)
				return status != MH_ERROR_NONE
					? status
					: mh_error_set(error_out,
						MH_ERROR_FAILED,
						"mixed next race exited");
		}
	}

	printf("mixed participation: late admission, individual Home return, live lobby standings, private chat, six-racer scoring and eight-player reload/start passed\n");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
run(const char *root, unsigned int track_number, const char *capture,
	struct mh_error *error_out)
{
	struct mh_net_session_config config;
	struct mh_car car;
	struct mh_track track;
	struct mh_net_session *session;
	struct mh_engine_input controls;
	struct mh_hud_state hud;
	struct mh_ui_list list;
	struct mh_audio_options audio;
	struct mh_drive_frame sampled;
	unsigned long previous_ticks;
	unsigned int i, j, frame, steps, wire, entry, total_steps;
	int done;
	double initial[8][3], moved;
	enum mh_error_code status;
	char results_path[1024];

	status = mh_track_config_load(root, track_number, &track, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 8; i++) {
		status = mh_car_config_load(
			root, (i + track_number - 1) % 10 + 1, &car, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		memset(&config, 0, sizeof(config));
		config.provider = 2;
		config.car_count = 10;
		config.laps = 2;
		strcpy(config.track, track.name);
		strcpy(config.profile.car, car.name);
		strcpy(config.profile.horn, i % 2 ? "Cow" : "Honker1");
		sprintf(config.profile.name, "NetDriver%u", i);
		config.profile.gear_mode = 1;
		config.profile.control_type = i % 5;
		sprintf(config.profile.team, "T%u", i);
		config.profile.values[0] = 0x123400UL + i * 0x1101UL;
		config.profile.update_rate = 15;
		config.grid_seed = 1998;
		status = mh_network_open(i == 0, "127.0.0.1", "127.0.0.1",
			i == 0 ? 1998 : mh_network_port(networks[0]), &config,
			&networks[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	mh_test_net_faults(13, 7, 9);
	status = wait_phase(MH_NET_LOBBY, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!mh_network_race_ready(mh_network_session(networks[0]), 8) ||
		mh_network_race_ready(mh_network_session(networks[1]), 8))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"incorrect frontend start readiness");

	status = mh_net_session_begin_race(mh_network_session(networks[0]),
		mh_network_now(networks[0]), error_out);

	if (status == MH_ERROR_NONE)
		status = wait_phase(MH_NET_LOADING, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 8; i++) {
		session = mh_network_session(networks[i]);
		status = mh_network_setup_load(
			root, session, 1, &contexts[i], &races[i], error_out);

		if (status == MH_ERROR_NONE)
			status = mh_network_setup_ui(root, session, &races[i],
				&contexts[i], error_out);

		if (status == MH_ERROR_NONE)
			status = mh_network_race_init(networks[i], &races[i],
				&contexts[i], &driving[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		memcpy(initial[i],
			races[i].field.cars[0].vehicle.motion.position,
			sizeof(initial[i]));

		for (j = 0; j < 8; j++) {
			const struct mh_results_assets *results;
			const struct mh_profile *profile;
			const struct mh_net_profile *player;

			wire = races[i].layout.player[j];
			results = &contexts[i].results;
			profile = &results->profiles[j];
			player = &session->roster.players[wire];

			if (strcmp(profile->name, player->name) != 0 ||
				strcmp(profile->nick, player->name) != 0 ||
				strcmp(profile->team, player->team) != 0 ||
				profile->colors[0] != player->values[0] ||
				memcmp(&results->portraits[j],
					&results->portraits[0],
					sizeof(results->portraits[0])) != 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"network results identity mismatch");
		}

		mh_hud_state(&races[i].field, 0, &hud, NULL);
		driving[i].race_hooks.hud(&driving[i], &hud);

		if (hud.message == NULL ||
			strcmp(hud.message, "WAITING FOR RACERS") != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"missing waiting HUD");
	}

	memset(&controls, 0, sizeof(controls));
	controls.throttle = .5;
	total_steps = 0;

	for (frame = 0; frame < 1500; frame++) {
		mh_test_net_advance(frame % 3 == 0 ? 16 : 17);

		for (i = 0; i < 8; i++) {
			races[i].field.cars[0].horn = frame >= 180 &&
				frame < 420 &&
				(mh_network_session(networks[i])->player & 1);
			status = driving[i].race_hooks.advance(&driving[i],
				&controls, 1.0 / 60, MH_FRAME_RUN, &steps,
				&done, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (done || steps > 2 || races[i].rejected_mask != 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"unexpected frontend frame result");

			total_steps += steps;
			frames++;
			session = mh_network_session(networks[i]);

			for (j = 1; j < 8; j++) {
				if (session->replication
						.history[races[i].layout.player
								[j]]
						.playback.ready)
					samples++;

				wire = races[i].layout.player[j];

				if ((frame == 360 &&
					races[i].field.cars[j].horn !=
						(int)(wire & 1)) ||
					(frame == 600 &&
						races[i].field.cars[j].horn))
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"frontend horn replication failed");
			}
		}
	}

	for (i = 0; i < 8; i++) {
		session = mh_network_session(networks[i]);

		if (session->phase != MH_NET_RACING || !races[i].started ||
			races[i].field.cars[0].race.now_ms < 5000)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"countdown did not progress through frontend");

		moved = 0;

		for (j = 0; j < 3; j++)
			moved += fabs(races[i].field.cars[0]
					.vehicle.motion.position[j] -
				initial[i][j]);

		if (moved < 1)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"frontend controlled car did not move");

		if (i != 0) {
			if (session->replication.status_revision == 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"no host status reached frontend");

			mh_hud_state(&races[i].field, 0, &hud, NULL);
			driving[i].race_hooks.hud(&driving[i], &hud);

			if (hud.position !=
					session->replication
						.status[session->player]
						.value_a ||
				hud.lap !=
					session->replication
						.status[session->player]
						.value_b)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"client HUD ignored host status");
		}

		if (!mh_results_draw(&contexts[i].results,
			&contexts[i].hud.atlas, &races[i].field.standings,
			960, 640, "Live race", &list))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"network results did not draw");

		status = info_overlay(i, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		printf("peer %u slot %u: %lu race ms, movement %.6f, %.6f RPM\n",
			i, session->player, races[i].field.cars[0].race.now_ms,
			moved, races[i].field.cars[0].vehicle.engine.rpm);
	}

	status = chat_burst(0, error_out);

	if (status == MH_ERROR_NONE)
		status = chat_burst(1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * A departure must reach every peer's renderer/results/physics mapping.
	 */
	wire = mh_network_session(networks[7])->player;
	status = mh_net_session_leave(
		mh_network_session(networks[7]), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (frame = 0; frame < 120; frame++) {
		mh_test_net_advance(17);
		status = pump(error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < 7; i++) {
			status = driving[i].race_hooks.advance(&driving[i],
				&controls, 1.0 / 60, MH_FRAME_RUN, &steps,
				&done, error_out);

			if (status != MH_ERROR_NONE || done)
				return status != MH_ERROR_NONE
					? status
					: mh_error_set(error_out,
						MH_ERROR_FAILED,
						"peer left closed race");
		}
	}

	mh_network_close(networks[7]);
	networks[7] = NULL;

	for (i = 0; i < 7; i++) {
		entry = races[i].layout.entry[wire];

		if ((races[i].field.active_mask & (1U << entry)) ||
			!(contexts[i].results.left_mask & (1U << entry)))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"departed car remains active");

		status = info_overlay(i, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (capture != NULL) {
		if (strlen(capture) > sizeof(results_path) - 13)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"capture path too long");

		memset(&audio, 0, sizeof(audio));
		sprintf(results_path, "%s-results.bmp", capture);

		if (!mh_race_run(&contexts[1], &races[1].field, capture, 1, 0,
			root, &audio, NULL, NULL) ||
			!mh_race_run(&contexts[1], &races[1].field,
				results_path, 1, 1, root, &audio, NULL, NULL))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"network scene/results capture failed");
	}

	/*
	 * Home follows original host leave-race/roster path, keeps transport
	 * alive.
	 */
	status = driving[0].race_hooks.restart(&driving[0], error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (frame = 0; frame < 200; frame++) {
		unsigned int ended;

		mh_test_net_advance(17);
		ended = 0;

		for (i = 0; i < 7; i++) {
			previous_ticks = races[i].field.cars[0].ticks;
			status = mh_drive_frame_sample(&races[i].field.cars[0],
				1.0 / 60, &sampled, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			status = driving[i].race_hooks.advance(&driving[i],
				&controls, 1.0 / 60, MH_FRAME_RUN, &steps,
				&done, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (done && driving[i].returned_to_lobby) {
				if (races[i].field.cars[0].frame.seconds !=
						sampled.seconds ||
					races[i].field.cars[0].ticks !=
						previous_ticks ||
					steps != 0)
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"lobby return did not retain its clock-only frame");

				ended++;
			}
		}

		if (ended == 7)
			break;
	}

	if (frame == 200)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"host restart did not return connected peers to lobby");

	status = wait_phase(MH_NET_LOBBY, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!mh_network_race_ready(mh_network_session(networks[0]), 7))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"host restart did not return connected peers to lobby");

	status = mh_net_session_begin_race(mh_network_session(networks[0]),
		mh_network_now(networks[0]), error_out);

	if (status == MH_ERROR_NONE)
		status = wait_phase(MH_NET_LOADING, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 7; i++) {
		mh_race_context_free(&contexts[i]);
		session = mh_network_session(networks[i]);
		status = mh_network_setup_load(
			root, session, 0, &contexts[i], &races[i], error_out);

		if (status == MH_ERROR_NONE)
			status = mh_network_setup_ui(root, session, &races[i],
				&contexts[i], error_out);

		if (status == MH_ERROR_NONE)
			status = mh_network_race_init(networks[i], &races[i],
				&contexts[i], &driving[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (races[i].field.count != 7 ||
			contexts[i].results.left_mask != 0 ||
			driving[i].returned_to_lobby || races[i].started ||
			races[i].field.cars[0].race.now_ms != 0 ||
			races[i].field.cars[0].start_ticks != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"second race retained departed entry or frontend state");

		if (i < 2 &&
			(strcmp((char *)driving[i]
					.race_hooks.chat->history.rows[0]
					.text,
				"Unsent draft") ||
				driving[i].race_hooks.chat->active ||
				driving[i].has_messages))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"race reload lost chat draft history or retained active/display state");
	}

	/*
	 * Loss can delay a reliable start beyond this five-second physics
	 * probe. Establish delivery separately, without advancing any car
	 * countdown.
	 */
	status = wait_phase(MH_NET_RACING, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (frame = 0; frame < 300; frame++) {
		mh_test_net_advance(17);

		for (i = 0; i < 7; i++) {
			status = driving[i].race_hooks.advance(&driving[i],
				&controls, 1.0 / 60, MH_FRAME_RUN, &steps,
				&done, error_out);

			if (status != MH_ERROR_NONE || done)
				return status != MH_ERROR_NONE
					? status
					: mh_error_set(error_out,
						MH_ERROR_FAILED,
						"second race frontend exited");
		}
	}

	for (i = 0; i < 7; i++)
		if (!races[i].started ||
			races[i].field.cars[0].race.now_ms != 0 ||
			races[i].field.cars[0].start_ticks >= 720) {
			fprintf(stderr,
				"second race peer %u: started %d, race %lu ms, countdown %u, phase %d\n",
				i, races[i].started,
				races[i].field.cars[0].race.now_ms,
				races[i].field.cars[0].start_ticks,
				(int)mh_network_session(networks[i])->phase);
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"second race reused first countdown/progress");
		}

	status = finish_round(0, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_net_session_begin_race(mh_network_session(networks[0]),
		mh_network_now(networks[0]), error_out);

	if (status == MH_ERROR_NONE)
		status = wait_phase(MH_NET_LOADING, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 7; i++) {
		mh_race_context_free(&contexts[i]);
		session = mh_network_session(networks[i]);
		status = mh_network_setup_load(
			root, session, 0, &contexts[i], &races[i], error_out);

		if (status == MH_ERROR_NONE)
			status = mh_network_setup_ui(root, session, &races[i],
				&contexts[i], error_out);

		if (status == MH_ERROR_NONE)
			status = mh_network_race_init(networks[i], &races[i],
				&contexts[i], &driving[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (races[i].finish.anchor_ms != 0 || races[i].ended ||
			session->notice_revision != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"new race retained finish countdown/notices");
	}

	for (frame = 0; frame < 300; frame++) {
		mh_test_net_advance(17);

		for (i = 0; i < 7; i++) {
			status = driving[i].race_hooks.advance(&driving[i],
				&controls, 1.0 / 60, MH_FRAME_RUN, &steps,
				&done, error_out);

			if (status != MH_ERROR_NONE || done)
				return status != MH_ERROR_NONE
					? status
					: mh_error_set(error_out,
						MH_ERROR_FAILED,
						"third race frontend exited");
		}
	}

	status = finish_round(1, 2, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = elimination_round(root, 0, 0, 3, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = elimination_round(root, 1, 0, 4, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = elimination_round(root, 1, 1, 5, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mixed_round(root, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	printf("track %u: %lu frontend frames, %lu ready samples, %u simulation steps, %lu native datagrams dropped; departure/reload/restart passed\n",
		track_number, frames, samples, total_steps,
		mh_test_net_drops());

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(int argc, char *argv[])
{
	unsigned int i, track;
	struct mh_error error;
	enum mh_error_code status;
	char failure[FAILURE_MESSAGE_BYTES];

	if ((argc != 3 && argc != 4) || strlen(argv[2]) != 1 ||
		argv[2][0] < '1' || argv[2][0] > '8') {
		fprintf(stderr,
			"Usage: mh-net-play-check GAME_ROOT TRACK_NUMBER [CAPTURE.bmp]\n");
		return 2;
	}

	track = (unsigned int)(argv[2][0] - '0');
	status = run(argv[1], track, argc == 4 ? argv[3] : NULL, &error);

	if (status != MH_ERROR_NONE)
		mh_error_copy(&error, &error, failure, sizeof(failure));

	for (i = 0; i < 8; i++) {
		mh_network_close(networks[i]);
		mh_race_context_free(&contexts[i]);
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	return 0;
}
