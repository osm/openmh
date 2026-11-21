#include <math.h>
#include <stddef.h>

#include "engine/simulation/motion_internal.h"

/*
 * Trigonometric values retain 64 significand bits. Matrix products and sums
 * round to 53 bits, then each component stores as binary32. Rounding the
 * trigonometric values to double first changes near-zero matrix components.
 * Pairs of doubles retain the extra bits without depending on the platform's
 * long-double format. These private operations only serve bounded road angles
 * (abs <= 100). Volatile intermediates force binary64 stores and prevent
 * multiply/add contraction or retention of excess evaluation precision.
 */
struct rotation_wide {
	double	hi, lo;
};

/* Renormalize a leading value and its smaller rounding residual. */
static struct rotation_wide
combine(double hi, double lo)
{
	struct rotation_wide r;
	volatile double sum, error, t;

	sum = hi + lo;
	t = sum - hi;
	error = lo - t;
	r.hi = sum;
	r.lo = error;

	return r;
}

static struct rotation_wide
add(struct rotation_wide a, struct rotation_wide b)
{
	volatile double sum, t, x, y, error;

	sum = a.hi + b.hi;
	t = sum - a.hi;
	x = sum - t;
	x = a.hi - x;
	y = b.hi - t;
	error = x + y;
	error += a.lo;
	error += b.lo;

	return combine(sum, error);
}

/* Split each leading significand into nonoverlapping 26/27-bit pieces. */
static struct rotation_wide
mul(struct rotation_wide a, struct rotation_wide b)
{
	volatile double product, split, ah, al, bh, bl, error, t;

	product = a.hi * b.hi;
	split = 134217729 * a.hi;
	t = split - a.hi;
	ah = split - t;
	al = a.hi - ah;
	split = 134217729 * b.hi;
	t = split - b.hi;
	bh = split - t;
	bl = b.hi - bh;
	error = ah * bh;
	error -= product;
	t = ah * bl;
	error += t;
	t = al * bh;
	error += t;
	t = al * bl;
	error += t;
	t = a.hi * b.lo;
	error += t;
	t = a.lo * b.hi;
	error += t;
	t = a.lo * b.lo;
	error += t;

	return combine(product, error);
}

static struct rotation_wide
divide(struct rotation_wide a, double b)
{
	struct rotation_wide residual;
	volatile double q, correction;

	q = a.hi / b;
	residual = add(a, mul(combine(-q, 0), combine(b, 0)));
	correction = residual.hi + residual.lo;
	correction /= b;

	return combine(q, correction);
}

static struct rotation_wide
neg(struct rotation_wide a)
{
	a.hi = -a.hi;
	a.lo = -a.lo;

	return a;
}

/* Round to a 64-bit significand, including powers of two and even ties. */
static struct rotation_wide
round64(struct rotation_wide a)
{
	double grid, units, n, fraction;
	int exponent;

	if (a.hi == 0)
		return a;

	fraction = frexp(a.hi, &exponent);

	if (fabs(fraction) == .5 &&
		((a.hi > 0 && a.lo < 0) || (a.hi < 0 && a.lo > 0)))
		exponent--;

	grid = ldexp(1.0, exponent - 64);
	units = a.lo / grid;
	n = floor(units);

	if (units > n + .5 || (units == n + .5 && fmod(n, 2) != 0))
		n += 1;

	return combine(a.hi, n * grid);
}

static void
rotation_trig(double angle, struct rotation_wide *c, struct rotation_wide *s)
{
	struct rotation_wide half_pi, x, xx, term, cosine, sine;
	int quadrant, n;

	if (angle == 0) {
		c->hi = 1;
		c->lo = 0;
		s->hi = angle;
		s->lo = 0;
		return;
	}

	/*
	 * Reduce to [-pi/4, pi/4] with a split pi/2, then evaluate 14 terms.
	 * The omitted terms are below 4e-33 over this interval.
	 * Small road angles converge earlier: after a term below 1e-36
	 * (relative to x for sine), the remaining tail is below 1e-36 too.
	 */
	half_pi = combine(
		1.570796326794896558, 6.123233995736766035868820147291e-17);
	quadrant = (int)(angle / half_pi.hi + (angle < 0 ? -.5 : .5));
	x = add(combine(angle, 0), mul(combine(-quadrant, 0), half_pi));
	xx = neg(mul(x, x));
	cosine = term = combine(1, 0);

	for (n = 1; n < 14; n++) {
		term = divide(mul(term, xx), (2 * n - 1) * (2 * n));
		cosine = add(cosine, term);

		if (fabs(term.hi) < 1e-36)
			break;
	}

	sine = term = x;

	for (n = 1; n < 14; n++) {
		term = divide(mul(term, xx), (2 * n) * (2 * n + 1));
		sine = add(sine, term);

		if (fabs(term.hi) < fabs(x.hi) * 1e-36)
			break;
	}

	cosine = round64(cosine);
	sine = round64(sine);

	switch ((quadrant % 4 + 4) % 4) {
	case 0:
		*c = cosine;
		*s = sine;
		break;

	case 1:
		*c = neg(sine);
		*s = cosine;
		break;

	case 2:
		*c = neg(cosine);
		*s = neg(sine);
		break;

	default:
		*c = sine;
		*s = neg(cosine);
		break;
	}
}

static double
rotation_product(struct rotation_wide a, double b)
{
	struct rotation_wide p;
	volatile double result;

	if (a.hi == 0 || b == 0)
		return a.hi * b;

	p = mul(a, combine(b, 0));
	result = p.hi + p.lo;

	return result;
}

static void
rotate_columns(double rotation[9], unsigned int axis,
	struct rotation_wide cosine, struct rotation_wide sine)
{
	double first, second;
	volatile double a, b, sum;
	unsigned int row, x, y;

	x = ((axis + 1) % 3) * 3;
	y = ((axis + 2) % 3) * 3;

	for (row = 0; row < 3; row++) {
		first = (double)(float)rotation[x + row];
		second = (double)(float)rotation[y + row];
		a = rotation_product(cosine, first);
		b = rotation_product(sine, second);
		sum = a + b;
		rotation[x + row] = (float)sum;
		a = rotation_product(cosine, second);
		b = rotation_product(sine, first);
		sum = a - b;
		rotation[y + row] = (float)sum;
	}
}

void
mh_motion_rotate_pair(
	double rotation[9], double other[9], const double angles[3])
{
	struct rotation_wide cosine, sine;
	unsigned int axis;

	/*
	 * Independent matrices share coefficients, each retaining XYZ stores.
	 */
	for (axis = 0; axis < 3; axis++) {
		rotation_trig((double)(float)angles[axis], &cosine, &sine);
		rotate_columns(rotation, axis, cosine, sine);

		if (other != NULL)
			rotate_columns(other, axis, cosine, sine);
	}
}

void
mh_motion_rotate(double rotation[9], const double angles[3])
{
	mh_motion_rotate_pair(rotation, NULL, angles);
}
