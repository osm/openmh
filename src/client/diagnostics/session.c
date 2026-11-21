#include <stdio.h>
#include <string.h>

#include "client/diagnostics/session.h"
#include "client/diagnostics/session_internal.h"

enum mh_debug_session
mh_debug_session_select(int argc, char *argv[])
{
	if (argc < 2)
		return MH_DEBUG_SESSION_NONE;

	if (strcmp(argv[1], "--debug-race") == 0)
		return MH_DEBUG_SESSION_RACE;

	if (strcmp(argv[1], "--debug-demo") == 0)
		return MH_DEBUG_SESSION_DEMO;

	if (strcmp(argv[1], "--debug-league") == 0)
		return MH_DEBUG_SESSION_LEAGUE;

	if (strcmp(argv[1], "--debug-network") == 0)
		return MH_DEBUG_SESSION_NETWORK;

	return MH_DEBUG_SESSION_NONE;
}

int
mh_debug_session_run(enum mh_debug_session session, int argc, char *argv[],
	const char *game_directory)
{
	/* The selector occupies argv[0] in the selected session's parser. */
	if (argc < 2)
		return 1;

	switch (session) {
	case MH_DEBUG_SESSION_RACE:
		return mh_debug_race_run(argc - 1, argv + 1, game_directory);
	case MH_DEBUG_SESSION_DEMO:
		return mh_debug_demo_run(argc - 1, argv + 1, game_directory);
	case MH_DEBUG_SESSION_LEAGUE:
#ifdef MH_DEBUG_LEAGUE
		return mh_debug_league_run(argc - 1, argv + 1, game_directory);
#else
		fputs("League diagnostics are unavailable in this build\n",
			stderr);
		return 1;

#endif
	case MH_DEBUG_SESSION_NETWORK:
#ifdef MH_DEBUG_NETWORK
		return mh_debug_network_run(argc - 1, argv + 1, game_directory);
#else
		fputs("Network diagnostics are unavailable in this build\n",
			stderr);
		return 1;

#endif
	default:
		return 1;
	}
}
