#ifndef MH_DECODED_ENV_SOUND_H
#define MH_DECODED_ENV_SOUND_H

#define MH_ENV_SOUNDS 64

enum mh_env_kind {
	MH_ENV_FIXED,
	MH_ENV_OBJECT,
	MH_ENV_COLLISION
};

struct mh_env_sound {
	enum mh_env_kind	kind;
	char			sample[64];

	/*
	 * ESD class selector, not a scene-object index. Unused for fixed
	 * sounds.
	 */
	unsigned int		object_class, priority;
	double	volume, pitch, near_distance, far_distance, position[3];
};

struct mh_env_sounds {
	struct mh_env_sound	sounds[MH_ENV_SOUNDS];
	unsigned int		count;
};

#endif
