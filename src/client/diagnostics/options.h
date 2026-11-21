#ifndef MH_CLIENT_DIAGNOSTICS_OPTIONS_H
#define MH_CLIENT_DIAGNOSTICS_OPTIONS_H

#include <stdio.h>

#include "engine/core/arguments.h"
#include "engine/core/error.h"

/* Startup-only diagnostics. Strings borrow argv; zero disables each option. */
struct mh_debug_options {
	unsigned int	window_titles, menu_clock, menu_frames, profile_trace;
	unsigned int	address_trace, lobby_trace, lobby_frames, network_events;
	unsigned int	camera_trace, hidden_trace, fixed_chase, glenz;
	unsigned int	music_trace, audio_trace, gpu;
	unsigned int	skip_loading_wait;
	int		texture_budget_set;
	unsigned long	texture_budget;
	const char	*lobby_capture, *screenshot_directory;
};

enum mh_argument_kind	mh_debug_argument_kind(const char *option);

/* Two arguments consumed on success; zero means unknown or invalid input. */
int	mh_debug_parse_option(struct mh_debug_options *debug,
	const char *option, const char *value);

/*
 * Development tools remove recognized debug arguments before their own parser.
 */
enum mh_error_code	mh_debug_parse_arguments(struct mh_debug_options *debug,
	int *argc, char *argv[], struct mh_error *error_out);
void			mh_debug_print_usage(FILE *output);
unsigned int		mh_debug_audio_trace(
	const struct mh_debug_options *debug);

const unsigned long	*mh_debug_texture_budget(
	const struct mh_debug_options *debug);

#endif
