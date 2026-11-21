#include <float.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/game/cheats.h"
#include "engine/simulation/suspension.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
check_suspension_bounds(void)
{
	struct mh_car car;
	struct mh_suspension_config config, saved;

	memset(&car, 0, sizeof(car));
	car.has_suspension = 1;
	car.spring_length = .5;
	car.spring_strength = 1000;
	CHECK(mh_suspension_configure(&car, MH_CHEAT_DEMON, &config, NULL) ==
		MH_ERROR_NONE);
	CHECK(config.length == 1.5 && config.strength == 160 &&
		config.damping == 21666);
	CHECK(mh_suspension_configure(&car, MH_CHEAT_G_RIDE, &config, NULL) ==
		MH_ERROR_NONE);
	CHECK(config.length == (double).6f && config.strength == 500 &&
		config.damping == -50000);
	saved = config;
	car.spring_length = 4000;
	CHECK(mh_suspension_configure(&car, MH_CHEAT_DEMON, &config, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&config, &saved, sizeof(config)) == 0);
	car.spring_length = 0;
	CHECK(mh_suspension_configure(&car, 0, &config, NULL) != MH_ERROR_NONE);
	car.spring_length = .5;
	car.spring_strength = DBL_MAX;
	CHECK(mh_suspension_configure(&car, 0, &config, NULL) != MH_ERROR_NONE);
	CHECK(mh_suspension_configure(NULL, 0, &config, NULL) != MH_ERROR_NONE);
	CHECK(mh_suspension_configure(&car, 0, NULL, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&config, &saved, sizeof(config)) == 0);
}

int
main(void)
{
	char name[16], team[4];

	check_suspension_bounds();
	CHECK(mh_cheat_flags("DeMoN", "GrEm") == MH_CHEAT_DEMON);
	CHECK(mh_cheat_flags("buzz aldrin", "nasa") == MH_CHEAT_MOON);
	CHECK(mh_cheat_flags("g-ride", "west") == MH_CHEAT_G_RIDE);
	CHECK(mh_cheat_flags("lemmy", "ace") == MH_CHEAT_THUNDER);
	CHECK(mh_cheat_flags("ramlosa", "h2o") == MH_CHEAT_UNDERWATER);
	CHECK(mh_cheat_flags("supercars", "grem") == MH_CHEAT_SUPERCARS);
	CHECK(mh_cheat_flags("ignition", "uds") == MH_CHEAT_IGNITION);
	CHECK(mh_cheat_flags("tribute to tron", NULL) == MH_CHEAT_TRON);
	CHECK(mh_cheat_flags("r3tr0", "a500") == MH_CHEAT_RETRO);
	CHECK(mh_cheat_flags("avenger", "zx") == 0); /* Separate map check. */
	CHECK(mh_cheat_flags("r pettersson", "swe") == 0);
	CHECK(mh_cheat_unlock_tracks("R Pettersson", "Swe"));
	CHECK(!mh_cheat_unlock_tracks("r pettersson", "swe!"));
	CHECK(mh_cheat_flags("buzz aldrin", "NASA extra") == MH_CHEAT_MOON);
	CHECK(mh_cheat_flags("lemmy", "ace!") == 0);
	CHECK(mh_cheat_flags("demon!", "grem") == 0);
	CHECK(mh_cheat_flags(" demon", "grem") == 0);
	CHECK(mh_cheat_flags(NULL, "grem") == 0);
	CHECK(mh_cheat_flags("demon", NULL) == 0);
	CHECK(!mh_cheat_unlock_tracks(NULL, NULL));
	memset(name, 'x', sizeof(name));
	memcpy(team, "grem", sizeof(team));
	CHECK(mh_cheat_flags(name, team) == 0);
	memcpy(name, "demon", 6);
	CHECK(mh_cheat_flags(name, team) == MH_CHEAT_DEMON);

	return failures != 0;
}
