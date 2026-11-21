#ifndef MH_NAVIGATION_AI_PATH_DATA_H
#define MH_NAVIGATION_AI_PATH_DATA_H

#include <stddef.h>

#define MH_AI_MAX_POINTS 100000UL
#define MH_AI_MAX_SECTIONS 99

struct mh_ai_point {
	double	x, z;

	/* Negative/positive lateral-side widths. */
	double	width[2];
	double	direction[3];

	/* Signed corner radius interpretation; zero straight. */
	double	radius;
	size_t	section;
};

struct mh_ai_section {
	/* Stored point count may be stale on reverse courses. */
	size_t	count;
	double	radius;
};

/*
 * Owned, decoded AI path in game coordinates.
 * Decoded data must remain unmodified while used by AI.
 */
struct mh_ai_path {
	size_t			count, section_count;
	struct mh_ai_point	*points;
	struct mh_ai_section	*sections;
};

#endif
