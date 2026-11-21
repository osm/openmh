#ifndef MH_RETRO_DATA_H
#define MH_RETRO_DATA_H

/*
 * Decoded effect data; all arrays are inline and independent of source bytes.
 */
enum {
	MH_RETRO_MESH_COUNT = 2,
	MH_RETRO_INSTANCE_COUNT = 5,
	MH_RETRO_PALETTE_COUNT = 2
};

enum {
	MH_RETRO_ROTATION_RATE = 0,
	MH_RETRO_BOB_OFFSET = 1,
	MH_RETRO_BOB_RATE = 2,
	MH_RETRO_MOTION_VECTORS = 3
};

#define MH_RETRO_LINES 145
#define MH_RETRO_LINE_BYTES 50
#define MH_RETRO_MAX_POINTS 86
#define MH_RETRO_MAX_FACES 96

struct mh_retro_face {
	unsigned int	count, indices[4], palette_index;
	float		normal[3];
};

struct mh_retro_mesh {
	unsigned int		point_count, face_count;
	float			points[MH_RETRO_MAX_POINTS][3];
	struct mh_retro_face	faces[MH_RETRO_MAX_FACES];
};

enum mh_retro_part {
	MH_RETRO_PART_MESHES,
	MH_RETRO_PART_ANIMATION,
	MH_RETRO_PART_PALETTE,
	MH_RETRO_PART_STORY,
	MH_RETRO_PART_COUNT
};

/* One independently supplied resource; its tag selects the active member. */
union mh_retro_part_data {
	struct mh_retro_mesh	meshes[MH_RETRO_MESH_COUNT];
	float			motion[MH_RETRO_INSTANCE_COUNT][MH_RETRO_MOTION_VECTORS][3];
	float			palette[MH_RETRO_PALETTE_COUNT][3];
	unsigned char		text[MH_RETRO_LINES][MH_RETRO_LINE_BYTES];
};

struct mh_retro_assets {
	struct mh_retro_mesh	meshes[MH_RETRO_MESH_COUNT];
	float			motion[MH_RETRO_INSTANCE_COUNT][MH_RETRO_MOTION_VECTORS][3];
	float			palette[MH_RETRO_PALETTE_COUNT][3];
	unsigned char		text[MH_RETRO_LINES][MH_RETRO_LINE_BYTES];
};

#endif
