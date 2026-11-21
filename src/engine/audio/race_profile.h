#ifndef MH_AUDIO_RACE_PROFILE_H
#define MH_AUDIO_RACE_PROFILE_H

/* Native race effects profiles; independent of the output device backend. */
enum mh_race_audio_profile {
	MH_RACE_AUDIO_SOFTWARE,
	MH_RACE_AUDIO_DS3D,
	MH_RACE_AUDIO_EAX,
	MH_RACE_AUDIO_A3D12,
	MH_RACE_AUDIO_A3D20
};

/* Stable saved IDs; availability is supplied by the build registry. */
int		 mh_race_audio_available(unsigned int selection);
unsigned int	 mh_race_audio_next(unsigned int selection, int direction);
unsigned int	 mh_race_audio_default(void);
const char	*mh_race_audio_name(unsigned int selection);

#endif
