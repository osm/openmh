#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/collision.h"

#define COORDINATE_LIMIT 1e6
#define PLANE_BIAS 0.08f
#define PLANE_TOLERANCE 0.08
#define EDGE_TOLERANCE 0.001
/* Largest binary32 below 2^31; leave room for negative-cell adjustment. */
#define GRID_COORDINATE_LIMIT 2147483520.0

struct segment_query {
	const struct mh_collision	*col;
	double				start[3];
	double				end[3];
	double				segment_length;
	double				grid_start[3];
	double				grid_end[3];
	struct mh_contact		best;
};

/*
 * Enumerate strict interior crossings, store their fractions and stably sort X,
 * Y, Z events. Merge those three ordered streams without allocating an event
 * buffer. Binary searches skip events before grid entry, so work is bounded by
 * grid dimensions even when the segment starts far outside the grid.
 */
struct cell_walk {
	double			start[3], delta[3], first[3], cell[3];
	unsigned long		count[3], next[3];
	const unsigned long	*dimensions;
	int			step[3];
};

static double
plane_distance(const double plane[4], const double point[3])
{
	/* D follows X, with binary53 intermediates. */
	return ((plane[0] * point[0] + plane[3]) + plane[1] * point[1]) +
		plane[2] * point[2];
}

static void
test_polygon(struct segment_query *query, unsigned long index)
{
	struct mh_collision_polygon polygon;
	double start_distance, end_distance, offset, bias, fraction;
	double point[3];
	unsigned int i;

	query->best.polygon_tests++;
	polygon = query->col->polygons[index];
	bias = PLANE_BIAS; /* The original arithmetic bias is binary32. */
	start_distance = plane_distance(polygon.plane, query->start);

	/* Store the distance but compare the unrounded extended value. */
	if (start_distance >= query->segment_length)
		return;

	start_distance = (float)start_distance;
	offset = 0;

	if (start_distance <= 0 && start_distance > -PLANE_TOLERANCE) {
		offset = bias;
		start_distance = (float)(start_distance + bias);
	}

	if (start_distance <= 0)
		return;

	end_distance =
		(float)(plane_distance(polygon.plane, query->end) + offset);

	if (end_distance > 0 && end_distance < PLANE_TOLERANCE)
		end_distance = (float)(end_distance - bias);

	if (end_distance > 0)
		return;

	fraction = start_distance / (start_distance - end_distance);

	/* Compare the register quotient with the stored winner. */
	if (query->best.hit && fraction >= query->best.fraction)
		return;

	/* Store the fraction, then every point construction step. */
	for (i = 0; i < 3; i++) {
		point[i] = (float)(query->end[i] - query->start[i]);
		point[i] = (float)(point[i] * (double)(float)fraction);
		point[i] = (float)(point[i] + query->start[i]);
		point[i] = (float)(point[i] + offset * polygon.plane[i]);
	}

	for (i = 0; i < polygon.boundary_count; i++) {
		if ((float)plane_distance(polygon.boundaries[i], point) <
			-EDGE_TOLERANCE)
			return;
	}

	query->best.hit = 1;
	query->best.polygon = index;
	query->best.material = polygon.material;
	query->best.fraction = (float)fraction;
	query->best.plane_distance = polygon.plane[3];
	memcpy(query->best.position, point, sizeof(point));
	memcpy(query->best.normal, polygon.plane, sizeof(query->best.normal));
}

static double
crossing_fraction(
	const struct cell_walk *walk, unsigned int axis, unsigned long index)
{
	volatile double value;

	value = walk->first[axis] + walk->step[axis] * (double)index;
	value -= walk->start[axis];
	value /= walk->delta[axis];

	return (float)value;
}

static int
cell_inside(const struct cell_walk *walk)
{
	unsigned int i;

	for (i = 0; i < 3; i++)
		if (walk->cell[i] < 0 || walk->cell[i] >= walk->dimensions[i])
			return 0;

	return 1;
}

static int
begin_walk(struct cell_walk *walk, const double start[3], const double end[3],
	const unsigned long dimensions[3])
{
	double target, fraction, required, count;
	unsigned long target_index, index, low, high, middle;
	unsigned int i, target_axis;
	int same_cell, needs_entry;

	memset(walk, 0, sizeof(*walk));
	walk->dimensions = dimensions;
	same_cell = 1;

	for (i = 0; i < 3; i++) {
		walk->start[i] = start[i];
		walk->cell[i] = start[i] < 0 ? ceil(start[i]) : floor(start[i]);

		if (walk->cell[i] !=
			(end[i] < 0 ? ceil(end[i]) : floor(end[i])))
			same_cell = 0;
	}

	/* The original same-cell shortcut precedes the negative adjustment. */
	if (same_cell)
		return cell_inside(walk);

	target = 0;
	target_axis = 0;
	target_index = 0;
	needs_entry = 0;

	for (i = 0; i < 3; i++) {
		if (start[i] < 0)
			walk->cell[i] -= 1;

		walk->delta[i] = (float)(end[i] - start[i]);
		walk->step[i] = walk->delta[i] > 0 ? 1 : -1;
		walk->first[i] = (float)(walk->cell[i] + (walk->step[i] > 0));
		count = ceil((end[i] - walk->first[i]) * walk->step[i]);

		if (walk->delta[i] != 0 && count > 0)
			walk->count[i] = (unsigned long)count;

		required = 0;

		if (walk->cell[i] < 0) {
			if (walk->step[i] < 0)
				return 0;

			required = -walk->cell[i];
		} else if (walk->cell[i] >= dimensions[i]) {
			if (walk->step[i] > 0)
				return 0;

			required = walk->cell[i] - dimensions[i] + 1;
		}

		if (required == 0)
			continue;

		if (required > walk->count[i])
			return 0;

		index = (unsigned long)required - 1;
		fraction = crossing_fraction(walk, i, index);

		if (!needs_entry || fraction > target ||
			(fraction == target && i > target_axis)) {
			target = fraction;
			target_axis = i;
			target_index = index;
		}

		needs_entry = 1;
	}

	if (!needs_entry)
		return 1;

	/*
	 * Advance through the latest entry event in the original stable order.
	 */
	for (i = 0; i < 3; i++) {
		low = 0;
		high = walk->count[i];

		while (low < high) {
			middle = low + (high - low) / 2;
			fraction = crossing_fraction(walk, i, middle);

			if (fraction < target ||
				(fraction == target &&
					(i < target_axis ||
						(i == target_axis &&
							middle <=
								target_index))))
				low = middle + 1;
			else
				high = middle;
		}

		walk->next[i] = low;
		walk->cell[i] += walk->step[i] * (double)low;
	}

	return cell_inside(walk);
}

static int
next_cell(struct cell_walk *walk)
{
	unsigned int i, axis;
	double fraction, best;

	axis = 3;
	best = 0;

	for (i = 0; i < 3; i++) {
		if (walk->next[i] == walk->count[i])
			continue;

		fraction = crossing_fraction(walk, i, walk->next[i]);

		if (axis == 3 || fraction < best) {
			axis = i;
			best = fraction;
		}
	}

	if (axis == 3)
		return 0;

	walk->next[axis]++;
	walk->cell[axis] += walk->step[axis];

	/* A monotone axis cannot reenter the grid after leaving it. */

	return cell_inside(walk);
}

static void
visit_cell(struct segment_query *query, const double coordinates[3])
{
	struct mh_collision_cell reference;
	const struct mh_collision_leaf *leaf;
	unsigned long cell[3];
	unsigned int i, depth, child;
	size_t root, index;

	for (i = 0; i < 3; i++)
		cell[i] = (unsigned long)coordinates[i];

	depth = query->col->depth;
	root = ((size_t)(cell[0] >> depth) * query->col->roots[1] +
		(cell[1] >> depth)) *
			query->col->roots[2] +
		(cell[2] >> depth);
	reference = query->col->cells[root];

	while (reference.kind == MH_COLLISION_NODE) {
		depth--;
		child = (unsigned int)((((cell[0] >> depth) & 1) << 2) |
			(((cell[1] >> depth) & 1) << 1) |
			((cell[2] >> depth) & 1));
		reference = query->col->nodes[reference.index].children[child];
	}

	if (reference.kind != MH_COLLISION_LEAF)
		return;

	leaf = &query->col->leaves[reference.index];

	for (index = 0; index < leaf->count; index++)
		test_polygon(query,
			query->col->polygon_indices[leaf->first + index]);
}

enum mh_error_code
mh_collision_cast(const struct mh_collision *col, const double start[3],
	const double end[3], struct mh_contact *contact,
	struct mh_error *error_out)
{
	struct segment_query query;
	struct cell_walk walk;
	unsigned int i;
	double delta;

	if (col == NULL || start == NULL || end == NULL || contact == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid collision query");

	memset(&query, 0, sizeof(query));
	query.col = col;
	query.best.fraction = 1;

	for (i = 0; i < 3; i++) {
		if (!(start[i] >= -COORDINATE_LIMIT &&
			start[i] <= COORDINATE_LIMIT) ||
			!(end[i] >= -COORDINATE_LIMIT &&
				end[i] <= COORDINATE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid collision coordinates");

		query.start[i] = (float)start[i];
		query.end[i] = (float)end[i];
		delta = query.end[i] - query.start[i];
		query.segment_length += delta * delta;

		/* Store translation and scaling separately. */
		query.grid_start[i] =
			(float)(query.start[i] + col->translation[i]);
		query.grid_end[i] = (float)(query.end[i] + col->translation[i]);
		query.grid_start[i] = (float)(query.grid_start[i] * col->scale);
		query.grid_end[i] = (float)(query.grid_end[i] * col->scale);

		/*
		 * Cell conversion requires representable signed 32-bit values.
		 */
		if (!(query.grid_start[i] >= -GRID_COORDINATE_LIMIT &&
			query.grid_start[i] <= GRID_COORDINATE_LIMIT &&
			query.grid_end[i] >= -GRID_COORDINATE_LIMIT &&
			query.grid_end[i] <= GRID_COORDINATE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"collision grid coordinates out of range");
	}

	/* Length and then biased rejection limit are stored. */
	query.segment_length = (float)sqrt(query.segment_length);

	if (query.segment_length != 0) {
		query.segment_length =
			(float)(query.segment_length + (double)PLANE_BIAS);

		if (begin_walk(&walk, query.grid_start, query.grid_end,
			col->dimensions)) {
			do {
				visit_cell(&query, walk.cell);
			} while (!query.best.hit && next_cell(&walk));
		}
	}

	*contact = query.best;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
