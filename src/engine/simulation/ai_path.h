#ifndef MH_AI_PATH_H
#define MH_AI_PATH_H

#include "engine/core/error.h"
#include "engine/navigation/ai_path.h"

struct mh_ai_location {
	size_t	point;
	double	distance;
	int	in_range;
};

struct mh_ai_cursor {
	size_t	point, corner;
	double	distance;
};

/*
 * Full XZ nearest-point search, with a 666-unit cutoff and point-zero fallback.
 * Y is deliberately absent. No allocation. Finite XZ bounds +/-1e6. Failure
 * preserves output.
 */
enum mh_error_code	mh_ai_locate(const struct mh_ai_path *path, double x,
	double z, struct mh_ai_location *output, struct mh_error *error_out);

/*
 * Walk forward, then backward, using squared XZ distances and the original
 * binary32 best-distance spills. Coordinates are sampled as binary32. If the
 * corner was the old current point, it follows each move. Parsed path is
 * borrowed; each walk is count-bounded to reject malformed nonconverging paths
 * without changing output.
 */
enum mh_error_code	mh_ai_follow(const struct mh_ai_path *path, double x,
	double z, size_t point, size_t corner, struct mh_ai_cursor *output,
	struct mh_error *error_out);

#endif
