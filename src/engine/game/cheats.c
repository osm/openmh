#include <stddef.h>

#include "engine/core/text.h"
#include "engine/game/cheats.h"

#define NAME_COMPARE_SIZE 16
#define TEAM_COMPARE_SIZE 4

struct cheat_code {
	const char	*name, *team;
	unsigned int	flags;
};

static int
matches_code(const char *text, const char *code, unsigned int count)
{
	return mh_ascii_equal_n(text == NULL ? "" : text, code, count);
}

unsigned int
mh_cheat_flags(const char *name, const char *team)
{
	static const struct cheat_code codes[] = {
		{"demon", "grem", MH_CHEAT_DEMON},
		{"buzz aldrin", "nasa", MH_CHEAT_MOON},
		{"g-ride", "west", MH_CHEAT_G_RIDE},
		{"lemmy", "ace", MH_CHEAT_THUNDER},
		{"ramlosa", "h2o", MH_CHEAT_UNDERWATER},
		{"supercars", "grem", MH_CHEAT_SUPERCARS},
		{"ignition", "uds", MH_CHEAT_IGNITION},
		{"tribute to tron", NULL, MH_CHEAT_TRON},
		{"r3tr0", "a500", MH_CHEAT_RETRO}};

	size_t i;

	for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
		if (matches_code(name, codes[i].name, NAME_COMPARE_SIZE) &&
			(codes[i].team == NULL ||
				matches_code(team, codes[i].team,
					TEAM_COMPARE_SIZE)))
			return codes[i].flags;

	return 0;
}

int
mh_cheat_unlock_tracks(const char *name, const char *team)
{
	return matches_code(name, "r pettersson", NAME_COMPARE_SIZE) &&
		matches_code(team, "swe", TEAM_COMPARE_SIZE);
}
