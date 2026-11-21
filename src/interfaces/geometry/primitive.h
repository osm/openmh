#ifndef MH_GEOMETRY_PRIMITIVE_H
#define MH_GEOMETRY_PRIMITIVE_H

enum {
	MH_GEOMETRY_FACE_TYPES = 10
};

#define MH_GEOMETRY_NO_TEXTURE (~0UL)

/* Decoded values shared by model and world geometry. No file offsets. */
struct mh_geometry_corner {
	double		position[3];
	double		normal[3];

	/* Pixel coordinates; original providers include the +0.5 bias. */
	double		uv[2];
	unsigned char	color[3];
};

struct mh_geometry_face {
	/* Retain collapsed exporter faces in order, but never render them. */
	int				degenerate;

	/* Three or four corners; zero is reserved for reader end-of-input. */
	unsigned int			count;

	/*
	 * Material modes: 0/1 flat, 2/3 textured, 4/5 corner colors,
	 * 6/7 textured corner colors, 8/9 corner normals. Each pair is
	 * triangle/quad. Renderer profiles interpret these modes and flags.
	 */
	unsigned int			type;
	unsigned int			flags;

	/*
	 * First corner color, retained for flat-face callers. RGB, not RGBA.
	 */
	unsigned char			color[3];

	/* Local-space visibility plane. */
	double				plane[4];

	/* Model texture-name index, or MH_GEOMETRY_NO_TEXTURE. */
	unsigned long			texture;
	struct mh_geometry_corner	corners[4];
};

struct mh_geometry_halo {
	/* Effect-table identity, not an image-collection index. */
	unsigned long	resource;
	double		endpoints[2][3];
	unsigned char	color[3];
};

#endif
