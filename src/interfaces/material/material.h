#ifndef MH_DECODED_MATERIAL_H
#define MH_DECODED_MATERIAL_H

#define MH_MATERIAL_LIMIT 64

enum mh_material_sound_kind {
	MH_MATERIAL_CONTACT,
	MH_MATERIAL_WEAK,
	MH_MATERIAL_HARD,
	MH_MATERIAL_SCRATCH,
	MH_MATERIAL_SLIDE,
	MH_MATERIAL_SPIN,
	MH_MATERIAL_SOUNDS
};

struct mh_material_sound {
	/* Empty means absent. Basename within Sounds/. */
	char		sample[32];
	unsigned long	flags, priority;

	/* Stored binary32, as in the original. */
	double		volume, pitch;

	/* Offsets measured in 16-bit samples; tail is excluded. */
	unsigned long	loop_start, loop_tail;
};

/* Surface physics, sounds and effect selectors. */
struct mh_material {
	char				name[32];
	double				spin_speed, spin_rot;
	double	x_spin, x_roll, z_spin, z_roll, rot_spin, rot_roll;
	double	turn_spin, turn_roll, bumpy, wheel_spin, wheel_roll;
	struct mh_material_sound	sounds[MH_MATERIAL_SOUNDS];
	unsigned long			slide_type;
	unsigned long			spark_anim, contact_anim, skid_mark;
};

struct mh_materials {
	unsigned int		count;
	unsigned int		surface_count;
	struct mh_material	entries[MH_MATERIAL_LIMIT];
};

#endif
