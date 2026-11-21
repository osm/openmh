#include "engine/game/lap_presets.h"
#include "engine/game/race_limits.h"

const unsigned int mh_single_lap_presets[MH_SINGLE_LAP_PRESET_COUNT] = {
	MH_RACE_MIN_LAPS, 3, 5, 10, 15, MH_RACE_MAX_LAPS};

static const unsigned int lobby_lap_presets[] = {
	MH_RACE_MIN_LAPS, 3, 5, 10, 15, 20, MH_RACE_MAX_LAPS};

static unsigned int
step_preset(const unsigned int *presets, unsigned int count, unsigned long laps,
	int forward)
{
	unsigned int i;

	if (forward) {
		for (i = 0; i < count; i++)
			if (presets[i] > laps)
				return presets[i];

		return presets[count - 1];
	}

	for (i = count; i != 0; i--)
		if (presets[i - 1] < laps)
			return presets[i - 1];

	return presets[0];
}

unsigned int
mh_single_lap_step(unsigned long laps, int forward)
{
	return step_preset(mh_single_lap_presets, MH_SINGLE_LAP_PRESET_COUNT,
		laps, forward);
}

unsigned long
mh_lobby_lap_step(unsigned long laps, int forward)
{
	if (laps < MH_RACE_MIN_LAPS || laps > MH_RACE_MAX_LAPS)
		return laps;

	return step_preset(lobby_lap_presets,
		sizeof(lobby_lap_presets) / sizeof(lobby_lap_presets[0]), laps,
		forward);
}
