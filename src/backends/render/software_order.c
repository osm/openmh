#include <stdlib.h>

#include "backends/render/software_material.h"
#include "backends/render/software_order.h"

#define DEPTH_LIMIT 1e9f
#define FACTOR_LIMIT 1e6f
#define CHILD_NEAR_MARGIN 5.1f
#define CHILD_FAR_MARGIN 4.9f
#define SHADOW_NEAR_MARGIN 5.75f
#define SHADOW_FAR_MARGIN 4.25f
#define TRIANGLE_AVERAGE 0.33333334f /* Binary32 bit pattern 0x3eaaaaab. */

/* Preserve each scene-precision operation. */
static struct mh_software_depth_band
depth_band(float near_margin, float far_margin, float inverse_range)
{
	struct mh_software_depth_band band;
	float near_offset, reserved;

	band.offset = (float)((double)far_margin * inverse_range);
	near_offset = (float)((double)near_margin * inverse_range);
	reserved = (float)((double)near_offset + band.offset);
	band.scale = (float)(1.0 - reserved);

	return band;
}

enum mh_error_code
mh_software_order_setup(float near_distance, float far_distance,
	enum mh_software_order_model model, struct mh_software_order *output,
	struct mh_error *error_out)
{
	/* Near/far margins from initialization. */
	static const float margins[2][MH_SOFTWARE_ORDER_MATERIALS][2] = {
		{{5, 5}, {4, 6}, {6, 4}}, {{10, 0}, {5, 5}, {15, 0}}};

	struct mh_software_order order;
	long double inverse_range;
	unsigned int mode, material;

	if (output == NULL ||
		(model != MH_SOFTWARE_ORDER_ROOT &&
			model != MH_SOFTWARE_ORDER_CHILD &&
			model != MH_SOFTWARE_ORDER_SHADOW) ||
		!(near_distance > 0 && near_distance <= DEPTH_LIMIT) ||
		!(far_distance > near_distance && far_distance <= DEPTH_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software ordering lens or model");

	order.buckets = MH_SOFTWARE_BUCKETS;
	order.near_distance = near_distance;

	/* Cache 1/(far-near) before scene preparation. */
	inverse_range = 1.0L / ((long double)far_distance - near_distance);

	if (!(inverse_range > 0 && inverse_range <= FACTOR_LIMIT))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software ordering lens range too small");

	order.inverse_range = (float)inverse_range;

	for (mode = 0; mode < 2; mode++)
		for (material = 0; material < MH_SOFTWARE_ORDER_MATERIALS;
			material++)
			order.bands[mode][material] =
				depth_band(margins[mode][material][0],
					margins[mode][material][1],
					order.inverse_range);

	/* Override all non-1 bands for attached parts. */
	if (model == MH_SOFTWARE_ORDER_CHILD)
		for (material = 0; material < MH_SOFTWARE_ORDER_MATERIALS;
			material++)
			order.bands[0][material] = depth_band(CHILD_NEAR_MARGIN,
				CHILD_FAR_MARGIN, order.inverse_range);

	/* Override the mode-1 plain band for shadows. */
	if (model == MH_SOFTWARE_ORDER_SHADOW)
		order.bands[1][MH_SOFTWARE_ORDER_PLAIN] =
			depth_band(SHADOW_NEAR_MARGIN, SHADOW_FAR_MARGIN,
				order.inverse_range);

	*output = order;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_order_bucket(const struct mh_software_order *order,
	const struct mh_software_point points[4], unsigned int count,
	unsigned int mode, unsigned int flags, unsigned int *bucket,
	struct mh_error *error_out)
{
	const struct mh_software_depth_band *band;
	unsigned int corner, material;
	float depth, sum, normalized, position;

	if (order == NULL || points == NULL || bucket == NULL ||
		(count != 3 && count != 4) ||
		flags > MH_SOFTWARE_MATERIAL_FLAG_MASK || order->buckets == 0 ||
		order->buckets > MH_SOFTWARE_BUCKETS ||
		!(order->near_distance > 0 &&
			order->near_distance <= DEPTH_LIMIT) ||
		!(order->inverse_range > 0 &&
			order->inverse_range <= FACTOR_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software ordering inputs");

	/* Bit 4 wins when both flags are present. */
	material = MH_SOFTWARE_ORDER_PLAIN;

	if (flags & MH_SOFTWARE_MATERIAL_DEPTH4)
		material = MH_SOFTWARE_ORDER_FLAG4;
	else if (flags & MH_SOFTWARE_MATERIAL_DEPTH128)
		material = MH_SOFTWARE_ORDER_FLAG128;

	band = &order->bands[mode == 1][material];

	if (!(band->offset >= -FACTOR_LIMIT && band->offset <= FACTOR_LIMIT) ||
		!(band->scale >= -FACTOR_LIMIT && band->scale <= FACTOR_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software depth band");

	sum = 0;

	for (corner = 0; corner < count; corner++) {
		if (points[corner].outcode &
			~(unsigned int)(MH_CLIP_VIEWPORT | MH_CLIP_NEAR))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"software ordering requires non-far points");

		depth = points[corner].outcode & MH_CLIP_NEAR
			? order->near_distance
			: points[corner].value[2];

		if (!(depth >= order->near_distance && depth <= DEPTH_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software ordering depth");

		sum = (float)((double)sum + depth);
	}

	depth = (float)((double)sum * (count == 3 ? TRIANGLE_AVERAGE : 0.25f));
	depth = (float)((double)depth - order->near_distance);
	normalized = (float)((double)depth * order->inverse_range);
	position = (float)(1.0 - normalized);
	position = (float)((double)position * band->scale);
	position = (float)((double)position + band->offset);
	position = (float)((double)position * (order->buckets - 1));

	/*
	 * Original truncates; reject unsafe table indices instead of clamping.
	 */
	if (!(position > -1 && position < order->buckets))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software face outside ordering buckets");

	*bucket = (unsigned int)(long)position;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
compare_spans(const void *left, const void *right)
{
	const struct mh_software_order_span *left_span, *right_span;

	left_span = left;
	right_span = right;

	if (left_span->bucket != right_span->bucket)
		return left_span->bucket < right_span->bucket ? -1 : 1;

	if (left_span->first != right_span->first)
		return left_span->first > right_span->first ? -1 : 1;

	return 0;
}

enum mh_error_code
mh_software_order_sort(struct mh_software_order_span *spans, size_t count,
	size_t triangle_count, unsigned int buckets, struct mh_error *error_out)
{
	size_t i, previous_end;

	if ((spans == NULL && count != 0) || buckets == 0 ||
		buckets > MH_SOFTWARE_BUCKETS ||
		count > (size_t)-1 / sizeof(*spans))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software ordering spans");

	previous_end = 0;

	for (i = 0; i < count; i++) {
		if (spans[i].bucket >= buckets || spans[i].count == 0 ||
			spans[i].first < previous_end ||
			spans[i].first > triangle_count ||
			spans[i].count > triangle_count - spans[i].first)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software source face span");

		previous_end = spans[i].first + spans[i].count;
	}

	if (count > 1)
		qsort(spans, count, sizeof(*spans), compare_spans);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
