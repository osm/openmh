#include <stdlib.h>
#include <string.h>

#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/collision/provider.h"
#include "backends/geometry/myo_provider.h"
#include "backends/geometry/myw_provider.h"
#include "backends/image/remap_provider.h"
#include "backends/material/provider.h"
#include "backends/navigation/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_config_load.h"
#include "client/diagnostics/asset_horn_catalog.h"
#include "client/diagnostics/options.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_loading.h"
#include "client/diagnostics/runtime_lobby.h"
#include "client/diagnostics/runtime_network.h"
#include "client/diagnostics/runtime_network_setup.h"
#include "client/diagnostics/runtime_race.h"
#include "client/diagnostics/runtime_records.h"
#include "client/diagnostics/session_internal.h"
#include "client/diagnostics/storage_controls.h"
#include "client/diagnostics/storage_settings_store.h"
#include "client/hosts/clock.h"
#include "client/hosts/storage.h"
#include "client/race/race.h"
#include "client/session/lobby.h"
#include "client/session/network_race.h"
#include "client/settings/controls.h"
#include "client/settings/render_choice.h"
#include "client/settings/store.h"
#include "engine/audio/horn.h"
#include "engine/audio/race_playback.h"
#include "engine/catalog/load.h"
#include "engine/core/arguments.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/simulation_rate.h"
#include "engine/game/mode.h"
#include "engine/game/race_limits.h"
#include "engine/input/control_type.h"
#include "engine/network/protocol_profile.h"
#include "engine/ui/lobby_session.h"
#include "interfaces/audio/horn_source.h"
#include "interfaces/storage/user_data_backend.h"

#define MAX_WAIT_SECONDS 86400UL
#define MILLISECONDS_PER_SECOND 1000.0

enum {
	LABEL_LENGTH = 15,
	ASCII_FIRST = 32,
	ASCII_LAST = 126,
	DEFAULT_PORT = MH_NET_LANINET_PORT,
	DEFAULT_WAIT_SECONDS = 120,
	NETWORK_POLL_MS = 10,
	ROSTER_GRACE_MS = 250,
	LEAVE_GRACE_MS = 250,
	MAX_PERCENT = 100,
	MAX_VOLUME = 255,
	BODY_COLOR_COUNT = 3,
	GRID_SEED = 1998
};

enum option_result {
	OPTIONS_INVALID,
	OPTIONS_HELP,
	OPTIONS_READY
};

struct network_options {
	const char	*asset_root, *address, *player_name, *password, *trace_path;
	const char	*horn, *records_path, *settings_path, *session_name;
	const char	*recordings_path, *controls_path, *music_root;
	unsigned long	port, required_players, car_number, track_number;
	unsigned long lap_count, timeout_seconds, effects_percent,
		music_percent;
	int		host, manual, scenery, mute, list_horns, elimination;
	int	no_records, no_settings, effects_override, music_override;
	int		terminal_lobby;
	int		network_legacy;
};

struct network_settings {
	struct mh_settings		defaults, *preferences;
	struct mh_settings_store	*store;
	struct mh_control_binding_set	bindings;
	unsigned long			random;
};

struct network_race_state {
	struct mh_race_context		context;
	struct mh_net_race		race;
	struct mh_network_race		controller;
	struct mh_highscore_history	scores;
};

static void
usage(void)
{
	puts("Usage: openmotor --debug-network (--debug-host BIND | --debug-join HOST)");
	puts("    [options]");
	puts("  --debug-records DIR | --debug-no-records   default records: per-user motorhead");
	puts("    data");
	puts("  --debug-recordings DIR              lobby /record output; default");
	puts("    userdata");
	puts("  --debug-settings CFG | --debug-no-settings   saved paint, graphics/audio and");
	puts("    HUD options");
	puts("  --debug-controls DIR   CLO overrides; settings enable saved bindings;");
	puts("    --debug-no-settings alone uses default Keyboard keys");
	puts("  --network-protocol NAME    select a protocol listed by openmotor --help");
	puts("  --debug-port 1..65535       LAN/INET UDP port (default 16000)");
	puts("  --debug-terminal-lobby     terminal lobby with automatic race startup");
	puts("  --debug-players 1..8       terminal host starts at this count (default 2,");
	puts("    including host)");
	printf("  --debug-car NUMBER --debug-track NUMBER --debug-laps %d..%d --debug-manual\n",
		MH_RACE_MIN_LAPS, MH_RACE_MAX_LAPS);
	puts("  --debug-elimination        host uses original checkpoint elimination rules");
	puts("  --debug-name NAME --debug-password TEXT   up to 15 printable ASCII characters");
	puts("  --debug-session NAME       session name advertised by the host (default");
	puts("    Motorhead)");
	puts("  --assets GAME_ROOT --debug-no-scenery --debug-wait 1..86400 --debug-trace");
	puts("    FILE");
	puts("  --debug-volume 0..100 --debug-music-volume 0..100 --debug-music-assets DIR");
	puts("    --debug-mute --debug-no-music");
	puts("Host controls track/laps/mode. Default: Kobra X3, RedRock, three laps,");
	puts("    automatic.");
	puts("Graphical lobby: type chat or /help; host F5 starts the race. Admission timeout:");
	puts("    120 seconds.");
	puts("F1 track; F2/F3 fewer/more laps; F4 clear scores; F6 car; F7 gears; F8 mode.");
	puts("Joining during a race waits for its completion; that wait has no time limit.");
	puts("Keyboard profile: arrows drive, S/A gears, Space horn, Shift handbrake, Ctrl");
	puts("    rear view, Esc menu.");
	puts("Without a saved profile, the same default Keyboard keys apply.");
	puts("  --debug-horn NAME (default Honker1); --debug-list-horns lists the CHF catalog.");
	puts("Hold Tab for Info; Esc opens the race menu.");
	puts("T: type chat; Enter: send; Esc: cancel. Keys 0..9 send saved Personal chat");
	puts("    shortcuts.");
	puts("Use the race menu to return to the lobby.");
	puts("The race continues while unfocused, input is released, or results are open.");
	puts("Completed rounds return to the lobby with cumulative race counts/points, then");
	puts("    start again.");
	puts("Native peers only; this does not connect to the original DirectPlay executable.");
	mh_debug_print_usage(stdout);
}

static int
valid_label(const char *text, int allow_empty)
{
	const unsigned char *cursor;

	if ((!allow_empty && *text == 0) || strlen(text) > LABEL_LENGTH)
		return 0;

	for (cursor = (const unsigned char *)text; *cursor != 0; cursor++)
		if (*cursor < ASCII_FIRST || *cursor > ASCII_LAST)
			return 0;

	return 1;
}

static void
print_name(const char *player_name)
{
	unsigned int i;
	unsigned char byte;

	for (i = 0; i < LABEL_LENGTH && player_name[i] != 0; i++) {
		byte = (unsigned char)player_name[i];
		putchar(byte >= ASCII_FIRST && byte <= ASCII_LAST ? byte : '?');
	}
}

static void
print_roster(const struct mh_net_session *session,
	const struct mh_lobby_roster *roster_view)
{
	unsigned int i, lobby_players;

	lobby_players = 0;

	for (i = 0; i < session->roster.player_count; i++)
		if (session->roster.players[i].flags & MH_NET_FLAG_PRESENT) {
			if (session->roster.players[i].flags &
				MH_NET_FLAG_LOBBY)
				lobby_players++;

			printf("%s %u: ",
				session->roster.players[i].flags &
						MH_NET_FLAG_RACING
					? "Racing"
					: "Lobby",
				i);
			print_name(roster_view->players[i].name);
			printf(" | %lu races | %lu points",
				roster_view->players[i].races,
				roster_view->players[i].points);

			if (roster_view->players[i].racing)
				printf(" | position %lu | lap %lu",
					roster_view->players[i].position,
					roster_view->players[i].lap);

			printf(" | ping %lu ms\n",
				roster_view->players[i].ping);
		}

	printf("Lobby: %u player%s%s\n", lobby_players,
		lobby_players == 1 ? "" : "s", session->host ? " (host)" : "");

	if (!session->host &&
		(session->roster.players[0].flags & MH_NET_FLAG_RACING))
		puts("Waiting for the current race to finish.");

	fflush(stdout);
}

static enum mh_error_code
wait_terminal_lobby(struct mh_network *network, unsigned int required_players,
	unsigned long timeout_seconds, struct mh_error *error_out)
{
	struct mh_net_session *session;
	struct mh_network_issue issue;
	struct mh_lobby_roster roster_view;
	enum mh_error_code status;
	unsigned long roster_revision, status_revision;
	double deadline_ms, ready_since_ms, now_ms;

	session = mh_network_session(network);
	roster_revision = status_revision = 0;
	ready_since_ms = -1;
	deadline_ms = mh_network_now(network) +
		timeout_seconds * MILLISECONDS_PER_SECOND;

	while (session->phase != MH_NET_LOADING) {
		mh_clock_delay(NETWORK_POLL_MS);
		status = mh_network_step(network, 0, &issue, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (issue.reason.code != MH_ERROR_NONE)
			fprintf(stderr, "Peer %lu: %s\n", issue.peer,
				issue.reason.message);

		if (session->phase == MH_NET_REJECTED)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"host rejected admission (check password/version/capacity)");

		if (session->phase == MH_NET_CLOSED)
			return session->kicked
				? mh_error_set(error_out, MH_ERROR_STATE,
					"removed from lobby")
				: mh_error_set(error_out, MH_ERROR_STATE,
					"connection closed");

		if (session->phase == MH_NET_LOBBY &&
			(roster_revision != session->roster_revision ||
				status_revision !=
					session->replication.status_revision)) {
			status = mh_lobby_session_roster(session,
				mh_network_name(network), 0, &roster_view,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;

			roster_revision = session->roster_revision;
			status_revision = session->replication.status_revision;
			print_roster(session, &roster_view);
		}

		now_ms = mh_network_now(network);

		if (!session->host && session->phase == MH_NET_LOBBY &&
			(session->roster.players[0].flags & MH_NET_FLAG_RACING))
			deadline_ms = now_ms +
				timeout_seconds * MILLISECONDS_PER_SECOND;

		if (mh_network_race_ready(session, required_players)) {
			if (ready_since_ms < 0)
				ready_since_ms = now_ms;

			/*
			 * Small native grace period for final roster delivery.
			 */
			if (now_ms - ready_since_ms >= ROSTER_GRACE_MS)
				return mh_net_session_begin_race(
					session, now_ms, error_out);
		} else
			ready_since_ms = -1;

		if (now_ms >= deadline_ms)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"timed out waiting for race participants");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
leave_network(struct mh_network *network)
{
	struct mh_net_session *session;
	struct mh_network_issue issue;
	double leave_deadline_ms;

	if (network == NULL)
		return;

	session = mh_network_session(network);

	if (session->phase == MH_NET_LOBBY ||
		session->phase == MH_NET_LOADING ||
		session->phase == MH_NET_WAITING ||
		session->phase == MH_NET_RACING)
		mh_net_session_leave(session, NULL);

	leave_deadline_ms = mh_network_now(network) + LEAVE_GRACE_MS;

	while (mh_network_now(network) < leave_deadline_ms) {
		mh_clock_delay(NETWORK_POLL_MS);

		if (mh_network_step(network, 0, &issue, NULL) != MH_ERROR_NONE)
			break;
	}
}

static enum option_result
parse_options(int argc, char *argv[], struct network_options *options,
	struct mh_debug_options *debug)
{
	int arg;
	int consumed;

	memset(debug, 0, sizeof(*debug));
	options->asset_root = NULL;
	options->address = options->trace_path = NULL;
	options->player_name = "Driver";
	options->password = "";
	options->host = -1;
	options->manual = options->mute = 0;
	options->scenery = 1;
	options->port = DEFAULT_PORT;
	options->required_players = 2;
	options->car_number = options->track_number = 1;
	options->lap_count = 3;
	options->timeout_seconds = DEFAULT_WAIT_SECONDS;
	options->effects_percent = options->music_percent = MAX_PERCENT;
	options->horn = "Honker1";
	options->list_horns = 0;
	options->elimination = 0;
	options->records_path = NULL;
	options->no_records = 0;
	options->settings_path = options->controls_path = NULL;
	options->no_settings = options->effects_override =
		options->music_override = 0;
	options->terminal_lobby = 0;
	options->network_legacy =
		mh_net_protocol_default() == MH_NET_PROTOCOL_LEGACY;
	options->session_name = "Motorhead";
	options->recordings_path = NULL;

	options->music_root = NULL;

	for (arg = 1; arg < argc; arg++) {
		consumed = mh_debug_parse_option(debug, argv[arg],
			arg + 1 < argc ? argv[arg + 1] : NULL);

		if (consumed != 0) {
			arg += consumed - 1;
			continue;
		}

		if (strcmp(argv[arg], "--help") == 0) {
			usage();
			return OPTIONS_HELP;
		}

		if (strcmp(argv[arg], "--network-protocol") == 0) {
			if (arg + 1 == argc ||
				(strcmp(argv[arg + 1], "openmh") &&
					strcmp(argv[arg + 1], "legacy")))
				return OPTIONS_INVALID;

			options->network_legacy =
				!strcmp(argv[++arg], "legacy");

			if (mh_net_protocol_get(options->network_legacy
					? MH_NET_PROTOCOL_LEGACY
					: MH_NET_PROTOCOL_OPENMH) == NULL)
				return OPTIONS_INVALID;

			continue;
		}

		if (strcmp(argv[arg], "--debug-terminal-lobby") == 0) {
			options->terminal_lobby = 1;
			continue;
		}

		if (strcmp(argv[arg], "--debug-manual") == 0) {
			options->manual = 1;
			continue;
		}

		if (strcmp(argv[arg], "--debug-no-records") == 0) {
			options->no_records = 1;
			continue;
		}

		if (strcmp(argv[arg], "--debug-no-settings") == 0) {
			options->no_settings = 1;
			continue;
		}

		if (strcmp(argv[arg], "--debug-elimination") == 0) {
			options->elimination = 1;
			continue;
		}

		if (strcmp(argv[arg], "--debug-no-scenery") == 0) {
			options->scenery = 0;
			continue;
		}

		if (strcmp(argv[arg], "--debug-mute") == 0) {
			options->mute = options->effects_override =
				options->music_override = 1;
			continue;
		}

		if (strcmp(argv[arg], "--debug-no-music") == 0) {
			options->music_percent = 0;
			options->music_override = 1;
			continue;
		}

		if (strcmp(argv[arg], "--debug-list-horns") == 0) {
			options->list_horns = 1;
			continue;
		}

		if (arg + 1 >= argc) {
			usage();
			return OPTIONS_INVALID;
		}

		if (strcmp(argv[arg], "--debug-host") == 0 ||
			strcmp(argv[arg], "--debug-join") == 0) {
			if (options->host != -1) {
				usage();
				return OPTIONS_INVALID;
			}

			options->host = strcmp(argv[arg], "--debug-host") == 0;
			options->address = argv[++arg];
		} else if (strcmp(argv[arg], "--debug-port") == 0) {
			if (!mh_arguments_unsigned(
				argv[++arg], MH_U16_MAX, &options->port) ||
				options->port == 0) {
				usage();
				return OPTIONS_INVALID;
			}
		} else if (strcmp(argv[arg], "--debug-players") == 0) {
			if (!mh_arguments_unsigned(argv[++arg], MH_NET_PLAYERS,
				&options->required_players) ||
				options->required_players == 0) {
				usage();
				return OPTIONS_INVALID;
			}
		} else if (strcmp(argv[arg], "--debug-car") == 0) {
			if (!mh_arguments_unsigned(argv[++arg],
				MH_CAR_CATALOG_LIMIT,
				&options->car_number) ||
				options->car_number == 0) {
				usage();
				return OPTIONS_INVALID;
			}
		} else if (strcmp(argv[arg], "--debug-track") == 0) {
			if (!mh_arguments_unsigned(argv[++arg],
				MH_TRACK_CATALOG_LIMIT,
				&options->track_number) ||
				options->track_number == 0) {
				usage();
				return OPTIONS_INVALID;
			}
		} else if (strcmp(argv[arg], "--debug-laps") == 0) {
			if (!mh_arguments_unsigned(argv[++arg],
				MH_RACE_MAX_LAPS, &options->lap_count) ||
				options->lap_count < MH_RACE_MIN_LAPS) {
				usage();
				return OPTIONS_INVALID;
			}
		} else if (strcmp(argv[arg], "--debug-wait") == 0) {
			if (!mh_arguments_unsigned(argv[++arg],
				MAX_WAIT_SECONDS,
				&options->timeout_seconds) ||
				options->timeout_seconds == 0) {
				usage();
				return OPTIONS_INVALID;
			}
		} else if (strcmp(argv[arg], "--debug-volume") == 0) {
			if (!mh_arguments_unsigned(argv[++arg], MAX_PERCENT,
				&options->effects_percent)) {
				usage();
				return OPTIONS_INVALID;
			}

			options->effects_override = 1;
		} else if (strcmp(argv[arg], "--debug-music-volume") == 0) {
			if (!mh_arguments_unsigned(argv[++arg], MAX_PERCENT,
				&options->music_percent)) {
				usage();
				return OPTIONS_INVALID;
			}

			options->music_override = 1;
		} else if (strcmp(argv[arg], "--assets") == 0)
			options->asset_root = argv[++arg];
		else if (strcmp(argv[arg], "--debug-name") == 0)
			options->player_name = argv[++arg];
		else if (strcmp(argv[arg], "--debug-session") == 0)
			options->session_name = argv[++arg];
		else if (strcmp(argv[arg], "--debug-password") == 0)
			options->password = argv[++arg];
		else if (strcmp(argv[arg], "--debug-trace") == 0)
			options->trace_path = argv[++arg];
		else if (strcmp(argv[arg], "--debug-music-assets") == 0)
			options->music_root = argv[++arg];
		else if (strcmp(argv[arg], "--debug-horn") == 0)
			options->horn = argv[++arg];
		else if (strcmp(argv[arg], "--debug-records") == 0)
			options->records_path = argv[++arg];
		else if (strcmp(argv[arg], "--debug-recordings") == 0)
			options->recordings_path = argv[++arg];
		else if (strcmp(argv[arg], "--debug-settings") == 0)
			options->settings_path = argv[++arg];
		else if (strcmp(argv[arg], "--debug-controls") == 0)
			options->controls_path = argv[++arg];
		else {
			usage();
			return OPTIONS_INVALID;
		}
	}

	return OPTIONS_READY;
}

static int
valid_options(const struct network_options *options)
{
	if (options->host == -1 || *options->address == 0 ||
		!valid_label(options->player_name, 0) ||
		!valid_label(options->password, 1) ||
		!valid_label(options->session_name, 1) ||
		(options->no_records && options->records_path != NULL) ||
		(options->no_settings && options->settings_path != NULL) ||
		(options->settings_path != NULL &&
			options->trace_path != NULL &&
			strcmp(options->settings_path, options->trace_path) ==
				0))
		return 0;

	return 1;
}

static unsigned int
settings_volume(unsigned long percent, int mute)
{
	return mute ? 0 : (unsigned int)(percent * MAX_VOLUME / MAX_PERCENT);
}

static enum mh_error_code
open_settings(const struct network_options *options,
	struct network_settings *settings, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_settings_init(&settings->defaults, options->player_name,
		settings_volume(options->effects_percent, options->mute),
		settings_volume(options->music_percent, options->mute),
		mh_render_choice_default(), error_out);

	if (status == MH_ERROR_NONE && !options->no_settings)
		status = mh_settings_store_open(options->asset_root,
			options->settings_path, &settings->defaults,
			&settings->random, &settings->store, error_out);

	return status;
}

static enum mh_error_code
configure_connection(const struct network_options *options,
	const struct network_settings *settings, const struct mh_car *car,
	const struct mh_track *track, const char *horn,
	struct mh_net_session_config *config, struct mh_error *error_out)
{
	const struct mh_settings *values;
	enum mh_error_code status;
	unsigned int i;

	memset(config, 0, sizeof(*config));
	config->provider = MH_NET_PROVIDER_LANINET;
	config->wire = MH_NET_WIRE_31_LOBBY;
	config->simulation_rate =
		options->network_legacy ? 0 : MH_SIM_RATE_DEFAULT;
	status = mh_car_catalog_count_from(mh_catalog_fallback_source(),
		mh_filesystem_resources(), options->asset_root,
		&config->car_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	values = settings->preferences == NULL ? &settings->defaults
					: settings->preferences;

	config->mode = options->elimination ? MH_NET_MODE_ELIMINATION
					: MH_NET_MODE_NORMAL;
	config->laps = (unsigned int)options->lap_count;
	strcpy(config->track, track->name);
	strcpy(config->password, options->password);
	strcpy(config->profile.name, options->player_name);
	strcpy(config->profile.car, car->name);
	strcpy(config->profile.horn, horn);
	config->profile.gear_mode = options->manual ? 0 : 1;
	config->profile.update_rate = values->tcp_rate;
	config->prediction = values->prediction;
	config->profile.control_type =
		settings->preferences != NULL || options->controls_path != NULL
		? (unsigned int)mh_control_type_update(
			settings->bindings.codes[MH_CONTROL_LEFT], 0)
		: MH_CONTROL_KEYBOARD;

	config->profile.values[MH_NET_PROFILE_COLOR] = values->player_color;

	for (i = 0; i < BODY_COLOR_COUNT; i++)
		config->profile.values[MH_NET_PROFILE_CAR_COLOR1 + i] =
			values->car_colors[i];

	config->grid_seed = GRID_SEED;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
reconnect_session(const struct network_options *options, unsigned int trace,
	struct mh_network **network, struct mh_net_session **session,
	struct mh_network_lobby *lobby, struct mh_error *error_out)
{
	struct mh_net_session_config config;
	struct mh_chat chat;
	enum mh_error_code status;

	config = (*session)->config;
	chat = *mh_network_chat_state(*network);
	leave_network(*network);
	mh_network_close(*network);
	*network = NULL;
	*session = NULL;
	status = mh_network_open(0, NULL, options->address,
		(unsigned int)options->port, &config, network, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_network_set_name(
			*network, options->session_name, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_network_set_trace(*network, trace);
	*session = mh_network_session(*network);
	*mh_network_chat_state(*network) = chat;
	mh_network_lobby_reconnected(lobby);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_race(const struct network_options *options,
	struct network_settings *settings, struct network_race_state *state,
	struct mh_network *network, struct mh_network_lobby *lobby,
	struct mh_diagnostic_loading *loading, struct mh_error *error_out)
{
	struct mh_net_session *session;
	enum mh_error_code status;

	session = mh_network_session(network);
	status = mh_network_setup_load_presented_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_catalog_fallback_source(), mh_navigation_mot_dat_source(),
		mh_animation_lightwave_source(), mh_atlas_remap_source(),
		mh_model_myo_decoder(), mh_world_myw_decoder(),
		mh_collision_fallback_source(), loading->options.sprite_source,
		loading->options.font_source, loading->options.image_source,
		loading->options.image_decoder,
		loading->options.portrait_decoder, loading->options.resources,
		options->asset_root, session, options->scenery,
		settings->preferences, &state->context, &state->race,
		&loading->screen,
		mh_debug_texture_budget(&loading->options.debug), error_out);

	if (status == MH_ERROR_NONE &&
		(settings->preferences != NULL ||
			options->controls_path != NULL))
		state->context.controls = &settings->bindings;

	if (status == MH_ERROR_NONE && settings->preferences != NULL)
		state->context.scene.visuals.particle_system.random =
			settings->random;

	if (status == MH_ERROR_NONE)
		status = mh_network_setup_ui_presented(options->asset_root,
			session, &state->race, &state->context,
			&loading->screen, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_network_race_init(network, &state->race,
			&state->context, &state->controller, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scores_bind(&state->context, MH_GAME_NETWORK,
			1, 1,
			session->roster.players[state->race.layout.player[0]]
				.name,
			&state->scores, error_out);

	if (status == MH_ERROR_NONE)
		mh_network_lobby_begin_race(lobby, &state->controller.messages);

	if (status == MH_ERROR_NONE)
		memcpy(state->controller.chat_config.shortcuts,
			settings->preferences == NULL
				? settings->defaults.short_keys
				: settings->preferences->short_keys,
			sizeof(state->controller.chat_config.shortcuts));

	if (status == MH_ERROR_NONE && !options->no_records)
		status = mh_race_records_open(&state->context,
			&state->race.field, options->records_path, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_network_race_record(&state->controller,
			mh_host_user_data(), options->asset_root,
			options->recordings_path,
			mh_network_lobby_recording(lobby), error_out);

	return status;
}

int
mh_debug_network_run(int argc, char *argv[], const char *game_directory)
{
	struct mh_debug_options debug;
	static struct network_race_state race_state;
	struct mh_net_session_config connection_config;
	struct mh_network *network;
	struct mh_network_lobby *lobby;
	enum mh_network_lobby_result lobby_result;
	struct mh_net_session *session;
	struct mh_track track_config;
	struct mh_car car_config;
	struct mh_audio_options audio_options;
	struct mh_horn_catalog horns;
	struct network_settings settings;
	struct mh_diagnostic_loading loading;
	struct mh_session_loading *presentation;
	struct mh_race_loop_options race_options;
	struct network_options options;
	enum option_result parsed;
	struct mh_error error;
	enum mh_error_code status;
	unsigned int horn_index, i, player;
	int exit_status, race_success;
	FILE *trace;

	status = mh_game_clock_start(&error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Game clock: %s\n", error.message);
		return 1;
	}

	parsed = parse_options(argc, argv, &options, &debug);

	if (parsed != OPTIONS_READY)
		return parsed == OPTIONS_HELP ? 0 : 2;

	if (options.asset_root == NULL)
		options.asset_root = game_directory;

	memset(&race_state.scores, 0, sizeof(race_state.scores));
	settings.store = NULL;
	settings.preferences = NULL;
	settings.random = 1;
	lobby = NULL;
	memset(&race_state.controller, 0, sizeof(race_state.controller));
	memset(&audio_options, 0, sizeof(audio_options));
	audio_options.music_root = options.music_root;

	status = mh_horn_catalog_load(options.asset_root, &horns, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Horns: %s\n", error.message);
		return 1;
	}

	if (options.list_horns) {
		for (i = 0; i < horns.count; i++)
			printf("%s\t%s\n", horns.horns[i].name,
				horns.horns[i].sample);

		return ferror(stdout) ? 1 : 0;
	}

	status = mh_horn_find(&horns, options.horn, &horn_index, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Horns: %s\n", error.message);
		return 1;
	}

	if (!valid_options(&options)) {
		usage();
		return 2;
	}

	status = mh_car_config_load(options.asset_root,
		(unsigned int)options.car_number, &car_config, &error);

	if (status == MH_ERROR_NONE)
		status = mh_track_config_load(options.asset_root,
			(unsigned int)options.track_number, &track_config,
			&error);

	if (status == MH_ERROR_NONE &&
		(!valid_label(car_config.name, 0) ||
			!valid_label(track_config.name, 0)))
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"selected catalog name does not fit the original network profile");

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Assets: %s\n", error.message);
		return 1;
	}

	status = open_settings(&options, &settings, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Settings: %s\n", error.message);
		return 1;
	}

	settings.preferences = mh_settings_store_values(settings.store);

	if (settings.preferences != NULL) {
		if (options.effects_override)
			settings.preferences->av.effects_volume =
				settings.defaults.av.effects_volume;

		if (options.music_override)
			settings.preferences->av.music_volume =
				settings.defaults.av.music_volume;
	}

	if (settings.preferences != NULL || options.controls_path != NULL) {
		status = mh_control_profiles_load(options.asset_root,
			options.controls_path, settings.preferences,
			&settings.bindings, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Controls: %s\n", error.message);
			mh_settings_store_close(settings.store);
			return 1;
		}
	}

	status = configure_connection(&options, &settings, &car_config,
		&track_config, horns.horns[horn_index].name, &connection_config,
		&error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Car catalog: %s\n", error.message);
		mh_settings_store_close(settings.store);
		return 1;
	}

	audio_options.effects_volume = options.mute
		? 0
		: options.effects_percent / (double)MAX_PERCENT;
	audio_options.music_volume =
		options.mute ? 0 : options.music_percent / (double)MAX_PERCENT;
	network = NULL;
	status = mh_network_open(options.host,
		options.host ? options.address : NULL,
		options.host ? NULL : options.address,
		(unsigned int)options.port, &connection_config, &network,
		&error);

	if (status == MH_ERROR_NONE && options.host)
		status = mh_network_setup_bind_cars(options.asset_root,
			mh_network_session(network), &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Network: %s\n", error.message);
		mh_network_close(network);
		mh_settings_store_close(settings.store);
		return 1;
	}

	mh_network_set_trace(network, debug.network_events);

	mh_diagnostic_loading_init(&loading, options.asset_root,
		options.no_records, options.records_path);
	loading.options.debug = debug;
	presentation = &loading.screen;
	session = mh_network_session(network);
	trace = NULL;
	exit_status = 0;
	status = mh_network_set_name(network, options.session_name, &error);

	if (options.trace_path != NULL) {
		trace = fopen(options.trace_path, "w");

		if (trace == NULL)
			status = mh_error_set(&error, MH_ERROR_IO,
				"cannot create driving trace");
	}

	if (status == MH_ERROR_NONE && !options.terminal_lobby)
		status = mh_network_lobby_open(options.asset_root, options.host,
			mh_game_clock_timer(), &debug, &lobby, &error);

	printf("%s %s UDP %lu; waiting for race setup\n",
		options.host ? "Hosting on" : "Joining", options.address,
		options.port);
	fflush(stdout);

	while (status == MH_ERROR_NONE) {
		if (options.terminal_lobby)
			status = wait_terminal_lobby(network,
				(unsigned int)options.required_players,
				options.timeout_seconds, &error);
		else {
			status = mh_network_lobby_run(lobby, network,
				options.timeout_seconds, &lobby_result, &error);

			if (status != MH_ERROR_NONE ||
				lobby_result == MH_LOBBY_LEAVE ||
				lobby_result == MH_LOBBY_QUIT)
				break;

			if (lobby_result == MH_LOBBY_RECONNECT_SESSION) {
				status = reconnect_session(&options,
					debug.network_events, &network,
					&session, lobby, &error);

				if (status != MH_ERROR_NONE)
					break;

				continue;
			}
		}

		if (status == MH_ERROR_NONE)
			status = prepare_race(&options, &settings, &race_state,
				network, lobby, &loading, &error);

		if (status != MH_ERROR_NONE)
			break;

		printf("Race loaded: %s, %u players, %u laps, %s. Esc opens the race menu.\n",
			race_state.context.scene.assets.track.name,
			race_state.race.field.count, session->roster.laps,
			session->roster.mode == MH_NET_MODE_ELIMINATION
				? "elimination"
				: "normal race");
		fflush(stdout);

		for (i = 0; i < race_state.race.field.count; i++) {
			player = race_state.race.layout.player[i];
			audio_options.horns[i] =
				session->roster.players[player].horn;
		}

		memset(&race_options, 0, sizeof(race_options));
		race_options.debug = debug;
		race_options.root = options.asset_root;
		race_options.audio_options = &audio_options;
		race_options.trace = trace;
		race_options.session = &race_state.controller.race_hooks;
		race_options.results =
			race_state.controller.race_hooks.initial_results;
		race_success = mh_diagnostic_race_run(&race_state.context,
			&race_state.race.field, &race_options, presentation);
		status = mh_network_race_record_end(
			&race_state.controller, race_success, &error);
		mh_network_lobby_history(lobby, &race_state.controller.messages,
			race_state.controller.chat_revision);

		if (status == MH_ERROR_NONE)
			status = mh_race_records_save(&race_state.context,
				&race_state.race.field, &error);

		if (status == MH_ERROR_NONE && race_success &&
			settings.store != NULL)
			status = mh_settings_store_save(settings.store, &error);

		mh_race_context_free(&race_state.context);

		if (status != MH_ERROR_NONE)
			break;

		if (!race_success) {
			exit_status = 1;
			break;
		}

		if (!race_state.controller.returned_to_lobby)
			break;
	}

	if (status == MH_ERROR_NONE && exit_status == 0 &&
		settings.preferences != NULL && session != NULL &&
		session->player < mh_net_session_capacity(session)) {
		settings.preferences->tcp_rate =
			session->config.profile.update_rate;
		settings.preferences->prediction = session->config.prediction;
		status = mh_settings_store_save(settings.store, &error);
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		exit_status = 1;
	}

	mh_session_loading_close(&loading.screen);
	mh_network_race_record_end(&race_state.controller, 0, NULL);
	mh_race_context_free(&race_state.context);
	leave_network(network);
	mh_network_close(network);
	mh_network_lobby_close(lobby);

	if (trace != NULL && fclose(trace) != 0) {
		fprintf(stderr, "Trace close failed\n");
		exit_status = 1;
	}

	mh_settings_store_close(settings.store);

	return exit_status;
}
