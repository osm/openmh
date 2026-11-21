#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/options.h"
#include "client/diagnostics/session_race_options.h"
#include "client/settings/render_choice.h"
#include "engine/audio/race_playback.h"
#include "engine/catalog/load.h"
#include "engine/core/arguments.h"
#include "engine/game/cheats.h"
#include "engine/game/race_limits.h"
#include "engine/simulation/ai_traffic.h"
#include "engine/simulation/race.h"

#define DIAGNOSTIC_TICK_LIMIT 100000UL

enum {
	MAX_GRID_SLOT = 63,
	DIFFICULTY_COUNT = 3,
	LABEL_LENGTH = 15,
	ASCII_FIRST = 32,
	ASCII_LAST = 126
};

void
mh_debug_race_usage(void)
{
	puts("Usage: openmotor --debug-race [--debug-car NUMBER] [--debug-track NUMBER]");
	puts("    [--assets GAME_ROOT]");
	puts("  --renderer NAME        select a renderer listed by openmotor --help");
	printf("  --debug-manual --debug-slot 0..63 --debug-laps %d..%d --debug-no-scenery\n",
		MH_RACE_MIN_LAPS, MH_RACE_MAX_LAPS);
	puts("    --debug-no-countdown");
	puts("  --debug-ai 1..3       autonomous controlled car using the track difficulty");
	puts("                   AI owns pedals/steering; pause and resets still work;");
	puts("    requires automatic gears");
	puts("  --debug-opponents 0..7 --debug-difficulty 1..3   add AI cars (default");
	puts("    difficulty 2)");
	puts("  --debug-catch-up       explicitly enable catch-up using this track's");
	puts("    AI strength");
	puts("  --debug-time-attack    solo race against personal and saved lap/race records;");
	puts("    grid slot 0");
	puts("  --debug-ticks N        simulation ticks before opening the window (AI drives");
	puts("    when selected)");
	puts("  --debug-replay FILE    then replay seven numbers/line: throttle brake steer up");
	puts("    down hand reset");
	puts("                   reset: 0 none, 1 route recovery, 2 starting grid; one tick");
	puts("    per line");
	puts("  --debug-trace FILE     write simulation states (one per replay tick /");
	puts("    interactive frame)");
	puts("  --debug-record-file FILE.MDE save an original-format demo on exit; never");
	puts("    replaces an existing file");
	puts("                   countdown required; team at most 4 characters; restarting discards");
	puts("    the current take");
	puts("  --debug-records DIR    existing record directory (interactive default:");
	puts("    per-user motorhead data)");
	puts("  --debug-no-records     disable records; diagnostics save only with");
	puts("    --debug-records DIR");
	puts("  --debug-settings FILE  native graphics/audio preferences (interactive default:");
	puts("    userdata/motorhead.cfg)");
	puts("  --debug-no-settings    disable saved preferences; explicit");
	puts("    --debug-volume/--debug-music-volume override saved values");
	puts("                   pause-menu options save on successful interactive exit;");
	puts("    captures/checks do not save");
	puts("  --debug-controls DIR   read CLO overrides; saved ControlName selects a profile");
	puts("    (default Keyboard)");
	puts("                   settings enable saved controls; --debug-no-settings retains");
	puts("    default Keyboard keys unless --debug-controls is given");
	puts("  --debug-results        open the results board (also works with captures)");
	puts("  --debug-name NAME      player name/nickname, 1..15 ASCII characters (default");
	puts("    Player)");
	puts("  --debug-team TEAM      separate team; enables name/team secrets");
	puts("                   Buzz Aldrin / NASA: moon gravity; Ignition / UDS: overhead");
	puts("    camera");
	puts("                   Supercars / Grem: fixed downward camera and simplified cars");
	puts("                   Demon / Grem: tall soft springs; G-ride / West: bouncing");
	puts("    suspension");
	puts("                   Ramlosa / H2O: underwater sky, waves and slower physics");
	puts("                   Lemmy / Ace: storm sky and lightning flashes");
	puts("                   tribute to tron (any team): blue hidden-line renderer and");
	puts("    introduction");
	puts("                   r3tr0 / a500: rotating meshes and curved demoscene story");
	puts("  --debug-volume 0..100 --debug-music-volume 0..100   separate volumes (defaults");
	puts("    100)");
	puts("  --debug-a3d12-audio    native A3D 1.2 stereo approximation");
	puts("  --debug-a3d20-audio    native A3D 2.0 occlusion and reflections");
	puts("  --debug-eax-audio      native EAX source policy and reverb approximation");
	puts("  --debug-spatial-audio  opt-in native DS3D distance, stereo direction and");
	puts("    Doppler");
	puts("  --debug-mute          disable all audio; --debug-no-music disables only music");
	puts("  --debug-music-assets DIR   CD track-NN.wav directory (default GAME_ROOT/Music)");
	puts("  --debug-horn NAME      CHF display name (default Honker1); --debug-list-horns");
	puts("    lists the catalog");
	puts("  --debug-headless BMP | --debug-capture BMP | --debug-check");
	puts("Keyboard profile: arrows drive; S/A gears; Space horn; Shift handbrake; Ctrl");
	puts("    rear view; Esc menu.");
	puts("Without a saved profile, the same default Keyboard keys apply.");
	puts("Esc pause menu; arrows/Enter navigate; hold Tab for Info.");
	puts("F1 in-car; F2 external; F3 track camera; hold Ctrl to look back.");
	puts("Losing focus pauses the session.");
	puts("Default: Kobra on Redrock, automatic gearbox, original hardware track and");
	puts("    scenery.");
	mh_debug_print_usage(stdout);
}

static void
init_options(struct mh_debug_race_options *options)
{
	options->asset_root = NULL;
	options->capture_path = options->replay_path = options->trace_path =
		NULL;
	options->car_number = options->track_number = 1;
	options->grid_slot = options->ticks = options->lap_count = 0;
	options->manual = options->headless = options->check = 0;
	options->scenery = 1;
	options->countdown = 1;
	options->spatial_audio = 0;
	options->ai_difficulty = options->opponent_count = 0;
	options->difficulty = 2;
	options->results = options->mute = options->list_horns =
		options->horn_selected = 0;
	options->player_name = "Player";
	options->horn = "Honker1";
	options->effects_percent = options->music_percent =
		MH_DEBUG_RACE_MAX_PERCENT;
	options->team = NULL;
	options->records_path = options->recording_path = NULL;
	options->no_records = options->time_attack = options->catch_up = 0;
	options->settings_path = options->controls_path = NULL;
	options->no_settings = options->effects_override =
		options->music_override = 0;

	options->music_root = NULL;
	options->renderer = MH_RENDER_CHOICE_COUNT;
}

enum mh_debug_race_parse_result
mh_debug_race_parse(int argc, char *argv[],
	struct mh_debug_race_options *options, struct mh_debug_options *debug)
{
	int i;
	int consumed;

	memset(debug, 0, sizeof(*debug));
	init_options(options);

	for (i = 1; i < argc; i++) {
		consumed = mh_debug_parse_option(
			debug, argv[i], i + 1 < argc ? argv[i + 1] : NULL);

		if (consumed != 0) {
			i += consumed - 1;
			continue;
		}

		if (strcmp(argv[i], "--help") == 0) {
			mh_debug_race_usage();
			return MH_DEBUG_RACE_HELP;
		} else if (strcmp(argv[i], "--renderer") == 0 && i + 1 < argc) {
			if (!mh_render_choice_parse(
				argv[++i], &options->renderer)) {
				mh_debug_race_usage();
				return MH_DEBUG_RACE_INVALID;
			}
		} else if (strcmp(argv[i], "--debug-manual") == 0)
			options->manual = 1;
		else if (strcmp(argv[i], "--debug-no-scenery") == 0)
			options->scenery = 0;
		else if (strcmp(argv[i], "--debug-no-countdown") == 0)
			options->countdown = 0;
		else if (strcmp(argv[i], "--debug-check") == 0)
			options->check = 1;
		else if (strcmp(argv[i], "--debug-no-records") == 0)
			options->no_records = 1;
		else if (strcmp(argv[i], "--debug-no-settings") == 0)
			options->no_settings = 1;
		else if (strcmp(argv[i], "--debug-settings") == 0 &&
			i + 1 < argc)
			options->settings_path = argv[++i];
		else if (strcmp(argv[i], "--debug-controls") == 0 &&
			i + 1 < argc)
			options->controls_path = argv[++i];
		else if (strcmp(argv[i], "--debug-time-attack") == 0)
			options->time_attack = 1;
		else if (strcmp(argv[i], "--debug-catch-up") == 0)
			options->catch_up = 1;
		else if (strcmp(argv[i], "--debug-records") == 0 &&
			i + 1 < argc)
			options->records_path = argv[++i];
		else if (strcmp(argv[i], "--debug-record-file") == 0 &&
			i + 1 < argc)
			options->recording_path = argv[++i];
		else if (strcmp(argv[i], "--debug-results") == 0)
			options->results = 1;
		else if (strcmp(argv[i], "--debug-spatial-audio") == 0)
			options->spatial_audio = MH_RACE_AUDIO_DS3D;
		else if (strcmp(argv[i], "--debug-a3d20-audio") == 0)
			options->spatial_audio = MH_RACE_AUDIO_A3D20;
		else if (strcmp(argv[i], "--debug-a3d12-audio") == 0)
			options->spatial_audio = MH_RACE_AUDIO_A3D12;
		else if (strcmp(argv[i], "--debug-eax-audio") == 0)
			options->spatial_audio = MH_RACE_AUDIO_EAX;
		else if (strcmp(argv[i], "--debug-mute") == 0) {
			options->mute = 1;
			options->effects_override = options->music_override = 1;
		} else if (strcmp(argv[i], "--debug-no-music") == 0) {
			options->music_percent = 0;
			options->music_override = 1;
		} else if (strcmp(argv[i], "--debug-list-horns") == 0)
			options->list_horns = 1;
		else if (strcmp(argv[i], "--debug-horn") == 0 && i + 1 < argc) {
			options->horn = argv[++i];
			options->horn_selected = 1;
		} else if (strcmp(argv[i], "--debug-music-assets") == 0 &&
			i + 1 < argc)
			options->music_root = argv[++i];
		else if (strcmp(argv[i], "--debug-music-volume") == 0 &&
			i + 1 < argc) {
			if (!mh_arguments_unsigned(argv[++i],
				MH_DEBUG_RACE_MAX_PERCENT,
				&options->music_percent)) {
				mh_debug_race_usage();
				return MH_DEBUG_RACE_INVALID;
			}

			options->music_override = 1;
		} else if (strcmp(argv[i], "--debug-volume") == 0 &&
			i + 1 < argc) {
			if (!mh_arguments_unsigned(argv[++i],
				MH_DEBUG_RACE_MAX_PERCENT,
				&options->effects_percent)) {
				mh_debug_race_usage();
				return MH_DEBUG_RACE_INVALID;
			}

			options->effects_override = 1;
		} else if (strcmp(argv[i], "--debug-name") == 0 && i + 1 < argc)
			options->player_name = argv[++i];
		else if (strcmp(argv[i], "--debug-team") == 0 && i + 1 < argc)
			options->team = argv[++i];
		else if (strcmp(argv[i], "--debug-opponents") == 0 &&
			i + 1 < argc) {
			if (!mh_arguments_unsigned(argv[++i],
				MH_TRAFFIC_MAX_CARS - 1,
				&options->opponent_count)) {
				mh_debug_race_usage();
				return MH_DEBUG_RACE_INVALID;
			}
		} else if (strcmp(argv[i], "--debug-difficulty") == 0 &&
			i + 1 < argc) {
			if (!mh_arguments_unsigned(argv[++i], DIFFICULTY_COUNT,
				&options->difficulty) ||
				options->difficulty == 0) {
				mh_debug_race_usage();
				return MH_DEBUG_RACE_INVALID;
			}
		} else if (strcmp(argv[i], "--debug-ai") == 0 && i + 1 < argc) {
			if (!mh_arguments_unsigned(argv[++i], DIFFICULTY_COUNT,
				&options->ai_difficulty) ||
				options->ai_difficulty == 0) {
				mh_debug_race_usage();
				return MH_DEBUG_RACE_INVALID;
			}
		} else if (strcmp(argv[i], "--debug-car") == 0 &&
			i + 1 < argc) {
			if (!mh_arguments_unsigned(argv[++i],
				MH_CAR_CATALOG_LIMIT,
				&options->car_number) ||
				options->car_number == 0) {
				mh_debug_race_usage();
				return MH_DEBUG_RACE_INVALID;
			}
		} else if (strcmp(argv[i], "--debug-track") == 0 &&
			i + 1 < argc) {
			if (!mh_arguments_unsigned(argv[++i],
				MH_TRACK_CATALOG_LIMIT,
				&options->track_number) ||
				options->track_number == 0) {
				mh_debug_race_usage();
				return MH_DEBUG_RACE_INVALID;
			}
		} else if (strcmp(argv[i], "--debug-slot") == 0 &&
			i + 1 < argc) {
			if (!mh_arguments_unsigned(argv[++i], MAX_GRID_SLOT,
				&options->grid_slot)) {
				mh_debug_race_usage();
				return MH_DEBUG_RACE_INVALID;
			}
		} else if (strcmp(argv[i], "--debug-laps") == 0 &&
			i + 1 < argc) {
			if (!mh_arguments_unsigned(argv[++i], MH_RACE_MAX_LAPS,
				&options->lap_count) ||
				options->lap_count < MH_RACE_MIN_LAPS) {
				mh_debug_race_usage();
				return MH_DEBUG_RACE_INVALID;
			}
		} else if (strcmp(argv[i], "--debug-ticks") == 0 &&
			i + 1 < argc) {
			if (!mh_arguments_unsigned(argv[++i],
				DIAGNOSTIC_TICK_LIMIT, &options->ticks)) {
				mh_debug_race_usage();
				return MH_DEBUG_RACE_INVALID;
			}
		} else if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc)
			options->asset_root = argv[++i];
		else if (strcmp(argv[i], "--debug-replay") == 0 && i + 1 < argc)
			options->replay_path = argv[++i];
		else if (strcmp(argv[i], "--debug-trace") == 0 && i + 1 < argc)
			options->trace_path = argv[++i];
		else if ((strcmp(argv[i], "--debug-headless") == 0 ||
				strcmp(argv[i], "--debug-capture") == 0) &&
			i + 1 < argc) {
			if (options->capture_path != NULL) {
				mh_debug_race_usage();
				return MH_DEBUG_RACE_INVALID;
			}

			options->headless =
				strcmp(argv[i], "--debug-headless") == 0;
			options->capture_path = argv[++i];
		} else {
			mh_debug_race_usage();
			return MH_DEBUG_RACE_INVALID;
		}
	}

	return MH_DEBUG_RACE_READY;
}

/* Compare supplied strings; CLI collision checks do not resolve aliases. */
static int
same_path(const char *left, const char *right)
{
	return left != NULL && right != NULL && strcmp(left, right) == 0;
}

int
mh_debug_race_validate(
	const struct mh_debug_race_options *options, unsigned int *output)
{
	const char *cursor;
	unsigned int cheats;
	int i;

	if (*options->player_name == 0 ||
		strlen(options->player_name) > LABEL_LENGTH ||
		(options->team != NULL &&
			(*options->team == 0 ||
				strlen(options->team) > LABEL_LENGTH))) {
		fprintf(stderr,
			"Name and team must contain 1..15 ASCII characters\n");
		return 0;
	}

	for (i = 0; i < 2; i++) {
		cursor = i == 0			? options->player_name
			: options->team == NULL ? ""
						: options->team;

		for (; *cursor != 0; cursor++)
			if ((unsigned char)*cursor < ASCII_FIRST ||
				(unsigned char)*cursor > ASCII_LAST) {
				fprintf(stderr,
					"Name and team must contain 1..15 ASCII characters\n");
				return 0;
			}
	}

	cheats = mh_cheat_flags(options->player_name, options->team);

	if (cheats != 0 && cheats != MH_CHEAT_MOON &&
		cheats != MH_CHEAT_IGNITION && cheats != MH_CHEAT_SUPERCARS &&
		cheats != MH_CHEAT_DEMON && cheats != MH_CHEAT_G_RIDE &&
		cheats != MH_CHEAT_UNDERWATER && cheats != MH_CHEAT_THUNDER &&
		cheats != MH_CHEAT_TRON && cheats != MH_CHEAT_RETRO) {
		fprintf(stderr,
			"This hidden mode is recognized but not connected yet\n");
		return 0;
	}

	if (options->time_attack &&
		(options->opponent_count != 0 || options->grid_slot != 0)) {
		fprintf(stderr,
			"--debug-time-attack requires --debug-opponents 0 and --debug-slot 0\n");
		return 0;
	}

	if (options->ai_difficulty != 0 && options->manual) {
		fprintf(stderr, "--debug-ai requires automatic transmission\n");
		return 0;
	}

	if (options->time_attack && options->catch_up) {
		fprintf(stderr,
			"--debug-catch-up is unavailable in time attack\n");
		return 0;
	}

	if (options->recording_path != NULL &&
		(!options->countdown ||
			(options->team != NULL &&
				strlen(options->team) >
					MH_DEBUG_RACE_TEAM_LENGTH))) {
		fprintf(stderr,
			"--debug-record-file requires countdown and a team of at most 4 characters\n");
		return 0;
	}

	if ((options->no_records && options->records_path != NULL) ||
		(options->no_settings && options->settings_path != NULL) ||
		(options->check &&
			(options->capture_path != NULL || options->results)) ||
		same_path(options->trace_path, options->replay_path) ||
		same_path(options->recording_path, options->trace_path) ||
		same_path(options->recording_path, options->replay_path) ||
		same_path(options->recording_path, options->capture_path)) {
		fprintf(stderr,
			"Conflicting output/input paths or capture/check modes\n");
		return 0;
	}

	*output = cheats;

	return 1;
}
