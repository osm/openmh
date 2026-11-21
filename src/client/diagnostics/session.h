#ifndef MH_CLIENT_DEBUG_SESSION_H
#define MH_CLIENT_DEBUG_SESSION_H

enum mh_debug_session {
	MH_DEBUG_SESSION_NONE,
	MH_DEBUG_SESSION_RACE,
	MH_DEBUG_SESSION_DEMO,
	MH_DEBUG_SESSION_LEAGUE,
	MH_DEBUG_SESSION_NETWORK
};

/* Select a session with the first option; following options belong to it. */
enum mh_debug_session	mh_debug_session_select(int argc, char *argv[]);

/* The host supplies its default game directory and owns final shutdown. */
int	mh_debug_session_run(enum mh_debug_session session, int argc,
	char *argv[], const char *game_directory);

#endif
