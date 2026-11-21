#include <math.h>

#include "engine/render/powervr_order.h"

#define REGION_SIZE MH_POWERVR_REGION_SIZE
#define WINDINGS MH_POWERVR_WINDINGS
#define NO_ITEM MH_POWERVR_NO_ITEM
#define DEPTH_SCALE .01f
#define PIXEL_OFFSET -.5f
#define WATER_AMPLITUDE 10.0f

static unsigned int
region(float position, unsigned int extent)
{
	if (position <= 0)
		return 0;

	if (position >= extent)
		return (extent - 1) / REGION_SIZE;

	return (unsigned int)position / REGION_SIZE;
}

int
mh_powervr_order_bounds(const struct mh_powervr_order_vertex vertices[3],
	unsigned int width, unsigned int height,
	const struct mh_water_frame *water,
	struct mh_powervr_order_triangle *triangle)
{
	const float *position;
	float x[3], y[3], w, inverse, determinant;
	float left, right, top, bottom, wave;
	unsigned int i;

	for (i = 0; i < 3; i++) {
		position = vertices[i].xy;
		w = vertices[i].w;

		/*
		 * Every PowerVR consumer supplies projected XY and reciprocal
		 * W.
		 */
		if (!(w > 0))
			return 0;

		inverse = (1 / w) * DEPTH_SCALE;

		if (i == 0 || inverse < triangle->depth)
			triangle->depth = inverse;

		x[i] = (position[0] + 1) * width * .5f;
		y[i] = (1 - position[1]) * height * .5f;

		if (water != NULL) {
			wave = (float)sin((y[i] - height) * water->frequency +
				water->phase) *
				WATER_AMPLITUDE;
			x[i] = width + (x[i] - width) * water->scale + wave;
			y[i] = height + (y[i] - height) * water->scale;
		}
	}

	determinant =
		(x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);

	if (determinant == 0)
		return 0;

	triangle->winding = determinant < 0;
	left = right = x[0];
	top = bottom = y[0];

	for (i = 1; i < 3; i++) {
		if (x[i] < left)
			left = x[i];

		if (x[i] > right)
			right = x[i];

		if (y[i] < top)
			top = y[i];

		if (y[i] > bottom)
			bottom = y[i];
	}

	if (right < 0 || bottom < 0 || left >= width || top >= height)
		return 0;

	/* SDK MODIFYXY subtracts half a pixel, then restores it for minima. */
	triangle->left = region(left, width);
	triangle->top = region(top, height);
	triangle->right = region(right + PIXEL_OFFSET, width);
	triangle->bottom = region(bottom + PIXEL_OFFSET, height);

	return 1;
}

static size_t
region_sets(struct mh_powervr_order_triangle *triangles, size_t triangle_count,
	struct mh_powervr_order_set *sets, size_t call_count, unsigned int x,
	unsigned int y, size_t heads[WINDINGS])
{
	struct mh_powervr_order_triangle *triangle;
	struct mh_powervr_order_set *set;
	size_t i, index, count;

	heads[0] = heads[1] = NO_ITEM;
	count = 0;

	for (i = 0; i < call_count * WINDINGS; i++)
		sets[i].first = NO_ITEM;

	for (i = 0; i < triangle_count; i++) {
		triangle = &triangles[i];

		if (x < triangle->left || x > triangle->right ||
			y < triangle->top || y > triangle->bottom)
			continue;

		index = triangle->call * WINDINGS + triangle->winding;
		set = &sets[index];
		triangle->next = NO_ITEM;

		if (set->first == NO_ITEM) {
			set->call = triangle->call;
			set->first = i;
			set->depth = triangle->depth;
			set->previous = heads[triangle->winding];
			heads[triangle->winding] = index;
			count++;
		} else {
			triangles[set->last].next = i;

			if (triangle->depth > set->depth)
				set->depth = triangle->depth;
		}

		set->last = i;
	}

	return count;
}

static size_t
sort_sets(struct mh_powervr_order_set *sets, size_t heads[WINDINGS])
{
	size_t head, tail, next, item, before;
	float depth;

	/* Preserve the SDK's front/back traversal and equal-depth insertion. */
	head = heads[0] != NO_ITEM ? heads[0] : heads[1];
	tail = head;
	next = sets[head].previous;
	sets[head].previous = sets[head].next = NO_ITEM;

	do {
		while (next != NO_ITEM) {
			item = next;
			next = sets[item].previous;
			depth = sets[item].depth;

			if (depth <= sets[head].depth) {
				sets[item].next = head;
				sets[item].previous = NO_ITEM;
				sets[head].previous = item;
				head = item;
			} else if (depth >= sets[tail].depth) {
				sets[item].next = NO_ITEM;
				sets[item].previous = tail;
				sets[tail].next = item;
				tail = item;
			} else {
				before = sets[head].next;

				while (depth > sets[before].depth)
					before = sets[before].next;

				sets[item].next = before;
				sets[item].previous = sets[before].previous;
				sets[sets[before].previous].next = item;
				sets[before].previous = item;
			}
		}

		if (heads[0] != NO_ITEM) {
			heads[0] = NO_ITEM;
			next = heads[1];
		}
	} while (next != NO_ITEM);

	return head;
}

size_t
mh_powervr_order_region(struct mh_powervr_order_triangle *triangles,
	size_t triangle_count, struct mh_powervr_order_set *sets,
	size_t call_count, unsigned int x, unsigned int y)
{
	size_t heads[WINDINGS];

	if (region_sets(triangles, triangle_count, sets, call_count, x, y,
		heads) == 0)
		return NO_ITEM;

	return sort_sets(sets, heads);
}
