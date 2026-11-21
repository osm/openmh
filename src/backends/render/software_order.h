#ifndef MH_SOFTWARE_ORDER_H
#define MH_SOFTWARE_ORDER_H

#include <stddef.h>

#include "backends/render/software_projection.h"

#define MH_SOFTWARE_BUCKETS 4096

enum mh_software_order_material {
	MH_SOFTWARE_ORDER_PLAIN,
	MH_SOFTWARE_ORDER_FLAG4,
	MH_SOFTWARE_ORDER_FLAG128,
	MH_SOFTWARE_ORDER_MATERIALS
};

struct mh_software_depth_band {
	float	offset, scale;
};

/* Cached producer settings; model traversal may temporarily replace bands. */
struct mh_software_order {
	unsigned int			buckets;
	float				near_distance, inverse_range;

	/* First bank for mode != 1, second for mode == 1. */
	struct mh_software_depth_band	bands[2][MH_SOFTWARE_ORDER_MATERIALS];
};

enum mh_software_order_model {
	MH_SOFTWARE_ORDER_ROOT,
	MH_SOFTWARE_ORDER_CHILD,
	MH_SOFTWARE_ORDER_SHADOW
};

/* A source face's contiguous prepared triangles, including clipped halves. */
struct mh_software_order_span {
	size_t		first, count;
	unsigned int	bucket;
};

/*
 * Build depth bands for a root model, attached child or shadow.
 * Lens cache construction uses extended precision; band arithmetic uses
 * scene binary32 precision. Both material-mode banks are initialized.
 * Attached children share one band when mode != 1. Shadows use separate
 * margins for the plain mode-1 band. Errors preserve output.
 */
enum mh_error_code	mh_software_order_setup(float near_distance,
	float far_distance, enum mh_software_order_model model,
	struct mh_software_order *output, struct mh_error *error_out);

/*
 * Original ordinary-face bucket selection, before near splitting. Points
 * have already passed far/common-outcode rejection. Near corners contribute
 * near_distance, regardless of their cached Z. Flags are the MYO flag byte.
 * All arithmetic rounds to scene binary32 precision. Errors preserve bucket.
 */
enum mh_error_code	mh_software_order_bucket(
	const struct mh_software_order *order,
	const struct mh_software_point points[4], unsigned int count,
	unsigned int mode, unsigned int flags, unsigned int *bucket,
	struct mh_error *error_out);

/*
 * Ascending buckets, reverse source insertion within each bucket. first is
 * also the insertion sequence: callers append prepared triangles in producer
 * order. Input spans must be nonoverlapping and ascending by first. Sorting
 * preserves each face's internal triangle order. Errors leave the array
 * unchanged. Spans refer to a separate triangle array.
 */
enum mh_error_code	mh_software_order_sort(
	struct mh_software_order_span *spans, size_t count,
	size_t triangle_count, unsigned int buckets,
	struct mh_error *error_out);

#endif
