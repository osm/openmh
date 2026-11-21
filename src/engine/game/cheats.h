#ifndef MH_CHEATS_H
#define MH_CHEATS_H

/*
 * Cheat mode flags. Identifying a mode does not implement its physics, graphics
 * or menu lifecycle.
 */
enum mh_cheat {
	MH_CHEAT_DEMON = 0x001,
	MH_CHEAT_MOON = 0x002,
	MH_CHEAT_G_RIDE = 0x004,
	MH_CHEAT_THUNDER = 0x008,
	MH_CHEAT_UNDERWATER = 0x010,
	MH_CHEAT_IGNITION = 0x020,
	MH_CHEAT_TRON = 0x040,
	MH_CHEAT_SUPERCARS = 0x080,
	MH_CHEAT_RETRO = 0x100
};

/*
 * ASCII case-insensitive comparisons of at most 16 name / 4 team bytes,
 * stopping at NUL. Four-letter teams accept suffixes as in the original.
 * NULL is treated as an empty string. Returns a fresh flag word, never
 * accumulates previous cheats. No allocation or mutable global state.
 */
unsigned int	mh_cheat_flags(const char *name, const char *team);

/*
 * Separate name/team trigger for making every track available. This predicate
 * alone does not unlock menus.
 */
int	mh_cheat_unlock_tracks(const char *name, const char *team);

#endif
