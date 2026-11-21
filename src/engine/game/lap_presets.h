#ifndef MH_GAME_LAP_PRESETS_H
#define MH_GAME_LAP_PRESETS_H

#define MH_SINGLE_LAP_PRESET_COUNT 6

extern const unsigned int mh_single_lap_presets[MH_SINGLE_LAP_PRESET_COUNT];

/* Single-player menus clamp out-of-range values to the nearest preset. */
unsigned int	mh_single_lap_step(unsigned long laps, int forward);

/* Lobby controls leave an out-of-range roster value alone. */
unsigned long	mh_lobby_lap_step(unsigned long laps, int forward);

#endif
