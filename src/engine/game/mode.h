#ifndef MH_GAME_MODE_H
#define MH_GAME_MODE_H

/*
 * Original game modes: CFG stores 0..3; runtime also uses 4..6. Keep the
 * explicit values when passing modes to game routines.
 */
enum mh_game_mode {
	MH_GAME_QUICK_RACE = 0,
	MH_GAME_LEAGUE = 1,
	MH_GAME_TIME_ATTACK = 2,
	MH_GAME_GHOST = 3,
	MH_GAME_NETWORK = 4,
	MH_GAME_ATTRACT = 5,
	MH_GAME_DEMO = 6
};

#endif
