#include <stddef.h>
#include <string.h>

#include "client/diagnostics/options.h"
#include "engine/audio/race_playback.h"

/* Largest supported profile budget; the scene loader checks its own limit. */
#define TEXTURE_BUDGET_MAX 33554432UL

static const struct {
	const char	*option;
	size_t		offset;
	const char	*description;
} toggles[] = {{"--debug-window-titles",
		offsetof(struct mh_debug_options, window_titles),
		"Show diagnostic window titles."},
	{"--debug-skip-loading-wait",
		offsetof(struct mh_debug_options, skip_loading_wait),
		"Continue after loading a local race."},
	{"--debug-menu-clock", offsetof(struct mh_debug_options, menu_clock),
		"Trace menu clock samples."},
	{"--debug-menu-frames", offsetof(struct mh_debug_options, menu_frames),
		"Trace menu animation frames."},
	{"--debug-profile-trace",
		offsetof(struct mh_debug_options, profile_trace),
		"Trace driver profiles and race rosters."},
	{"--debug-address-trace",
		offsetof(struct mh_debug_options, address_trace),
		"Trace multiplayer address-book reloads."},
	{"--debug-lobby-trace", offsetof(struct mh_debug_options, lobby_trace),
		"Trace lobby state changes."},
	{"--debug-lobby-frames",
		offsetof(struct mh_debug_options, lobby_frames),
		"Trace lobby frames."},
	{"--debug-network-events",
		offsetof(struct mh_debug_options, network_events),
		"Trace network events."},
	{"--debug-camera-trace",
		offsetof(struct mh_debug_options, camera_trace),
		"Trace race timing and camera poses."},
	{"--debug-hidden-trace",
		offsetof(struct mh_debug_options, hidden_trace),
		"Trace hidden-effect timing."},
	{"--debug-fixed-chase", offsetof(struct mh_debug_options, fixed_chase),
		"Use the diagnostic fixed chase camera."},
	{"--debug-glenz", offsetof(struct mh_debug_options, glenz),
		"Force Glenz on hardware profiles."},
	{"--debug-music-trace", offsetof(struct mh_debug_options, music_trace),
		"Trace music handoffs and playback cursors."},
	{"--debug-audio-trace", offsetof(struct mh_debug_options, audio_trace),
		"Trace audio playback phases."},
	{"--debug-gpu", offsetof(struct mh_debug_options, gpu),
		"Enable GPU validation and driver diagnostics."}};

enum mh_argument_kind
mh_debug_argument_kind(const char *option)
{
	size_t i;

	for (i = 0; i < sizeof(toggles) / sizeof(toggles[0]); i++) {
		if (strcmp(option, toggles[i].option) == 0)
			return MH_ARGUMENT_VALUE;
	}

	if (strcmp(option, "--debug-texture-bytes") == 0 ||
		strcmp(option, "--debug-lobby-capture") == 0 ||
		strcmp(option, "--debug-screenshots") == 0)
		return MH_ARGUMENT_VALUE;

	return MH_ARGUMENT_UNKNOWN;
}

int
mh_debug_parse_option(
	struct mh_debug_options *debug, const char *option, const char *value)
{
	unsigned int i, *toggle;

	if (value == NULL)
		return 0;

	for (i = 0; i < sizeof(toggles) / sizeof(toggles[0]); i++) {
		if (strcmp(option, toggles[i].option) != 0)
			continue;

		if (strcmp(value, "0") != 0 && strcmp(value, "1") != 0)
			return 0;

		toggle = (unsigned int *)((unsigned char *)debug +
			toggles[i].offset);
		*toggle = value[0] == '1';

		return 2;
	}

	if (strcmp(option, "--debug-texture-bytes") == 0) {
		if (!mh_arguments_unsigned(
			value, TEXTURE_BUDGET_MAX, &debug->texture_budget))
			return 0;

		debug->texture_budget_set = 1;
	} else if (strcmp(option, "--debug-lobby-capture") == 0 && *value != 0)
		debug->lobby_capture = value;
	else if (strcmp(option, "--debug-screenshots") == 0 && *value != 0)
		debug->screenshot_directory = value;
	else
		return 0;

	return 2;
}

void
mh_debug_print_usage(FILE *output)
{
	unsigned int i;

	fputs("\nDevelopment: diagnostic toggles (0 off, 1 on)\n", output);

	for (i = 0; i < sizeof(toggles) / sizeof(toggles[0]); i++)
		fprintf(output, "  %-27s 0|1  %s\n", toggles[i].option,
			toggles[i].description);

	fputs("\nDevelopment: diagnostic limits and output\n"
	"  --debug-texture-bytes N          Override logical texture capacity in bytes.\n"
	"                                   Maximum: Glide 4194304, Direct3D 33554432,\n"
	"                                   PowerVR 6291456. Zero is allowed.\n"
	"  --debug-lobby-capture FILE       Save lobby diagnostic captures.\n"
	"  --debug-screenshots DIR          Override the screenshot output directory.\n",
		output);
}

unsigned int
mh_debug_audio_trace(const struct mh_debug_options *debug)
{
	return (debug->music_trace ? MH_AUDIO_TRACE_MUSIC : 0) |
		(debug->audio_trace ? MH_AUDIO_TRACE_PHASES : 0);
}

enum mh_error_code
mh_debug_parse_arguments(struct mh_debug_options *debug, int *argc,
	char *argv[], struct mh_error *error_out)
{
	int source, destination;

	memset(debug, 0, sizeof(*debug));
	destination = 1;

	for (source = 1; source < *argc; source++) {
		if (strncmp(argv[source], "--debug-", 8) != 0) {
			argv[destination++] = argv[source];
			continue;
		}

		if (source + 1 == *argc ||
			!mh_debug_parse_option(
				debug, argv[source], argv[source + 1]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unknown debug option or invalid value");

		source++;
	}

	*argc = destination;
	argv[destination] = NULL;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const unsigned long *
mh_debug_texture_budget(const struct mh_debug_options *debug)
{
	return debug->texture_budget_set ? &debug->texture_budget : NULL;
}
