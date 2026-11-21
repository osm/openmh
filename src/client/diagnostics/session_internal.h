#ifndef MH_CLIENT_DEBUG_SESSION_INTERNAL_H
#define MH_CLIENT_DEBUG_SESSION_INTERNAL_H

int	mh_debug_race_run(int argc, char *argv[], const char *game_directory);
int	mh_debug_demo_run(int argc, char *argv[], const char *game_directory);
int	mh_debug_league_run(int argc, char *argv[], const char *game_directory);
int	mh_debug_network_run(int argc, char *argv[],
	const char *game_directory);

#endif
