#include <stddef.h>

#include "engine/audio/race_profile.h"

const char *
mh_race_audio_name(unsigned int selection)
{
	static const char *const names[] = {
		"Software", "DirectSound 3D", "EAX", "A3D 1.2", "A3D 2.0"};

	return selection <= MH_RACE_AUDIO_A3D20 ? names[selection] : NULL;
}

unsigned int
mh_race_audio_next(unsigned int selection, int direction)
{
	unsigned int i;

	for (i = 0; i <= MH_RACE_AUDIO_A3D20; i++) {
		if (direction < 0)
			selection = selection == 0 ? MH_RACE_AUDIO_A3D20
						: selection - 1;
		else
			selection = selection >= MH_RACE_AUDIO_A3D20
				? 0
				: selection + 1;

		if (mh_race_audio_available(selection))
			return selection;
	}

	return selection;
}

unsigned int
mh_race_audio_default(void)
{
	return mh_race_audio_next(MH_RACE_AUDIO_A3D20, 1);
}
