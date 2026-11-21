#ifndef MH_SKIDS_H
#define MH_SKIDS_H

#include "engine/core/error.h"
#define MH_SKID_SEGMENTS 50

struct mh_skid_segment {
	float	start[3], end[3], normal[3], color[3];
};

struct mh_skid_wheel {
	int			active, committed, partial, oldest_visible;
	unsigned int		first, next, previous, count;
	struct mh_skid_segment	segments[MH_SKID_SEGMENTS];
};

struct mh_skids {
	struct mh_skid_wheel	wheels[4];
};

struct mh_skid_contact {
	int		supported;

	/* SRF-ordered material SkidMark; only 1..4 emit. */
	unsigned long	mark;
	float		attachment[3], position[3], normal[3];
};

struct mh_skid_input {
	struct mh_skid_contact	wheels[4];

	/* Render transform, column-major. */
	float			rotation[9], position[3];

	/* Internal transform Y saved at physics tick entry. */
	float			contact_origin_y;
	float			velocity_x, velocity_z, slide_amount;
	int	wheelspin, heavy_brake, viewed, in_car, disabled;
};

/*
 * One update per rendered frame, after physics. No time fade: each wheel
 * retains at most 50 completed segments plus a live end. Renderer supplies
 * oldest_visible; a full visible non-viewed history stops further emission.
 * Whole output unchanged on error; in-place is supported. Native bounds:
 * positions/attachments <= 1e6, velocity <= 1e4, slide <= 1e6, rotation/normal
 * components <= 2.
 */
enum mh_error_code	mh_skids_update(const struct mh_skid_input *input,
	const struct mh_skids *state, struct mh_skids *output,
	struct mh_error *error_out);
void			mh_skids_reset(struct mh_skids *state);

#endif
