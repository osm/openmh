#ifndef MH_AUDIO_HORN_DATA_H
#define MH_AUDIO_HORN_DATA_H

#define MH_HORNS 128

struct mh_horn {
	char	name[16], sample[128];

	/* Binary32 factors: 1 + percentage * .01. */
	double	volume, pitch;
};

struct mh_horn_catalog {
	unsigned int	count;
	struct mh_horn	horns[MH_HORNS];
};

#endif
