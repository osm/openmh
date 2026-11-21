#include <float.h>
#include <limits.h>
#include <math.h>

#include "engine/core/binary.h"
#include "engine/core/limits.h"

#if CHAR_BIT != 8
#error Motorhead asset readers require 8-bit bytes
#endif

enum {
	F32_FRACTION_BITS = 23,
	F32_EXPONENT_BIAS = 127,
	F32_SPECIAL_EXPONENT = 255,
	F64_HIGH_FRACTION_BITS = 20,
	F64_FRACTION_BITS = 52,
	F64_EXPONENT_BIAS = 1023,
	F64_SPECIAL_EXPONENT = 2047
};

#define F32_FRACTION_MASK 0x007fffffUL
#define F32_IMPLICIT_BIT 0x00800000UL
#define F32_EXPONENT_MASK 0xffUL
#define F64_HIGH_FRACTION_MASK 0xfffffUL
#define F64_EXPONENT_MASK 0x7ffUL
#define F64_FRACTION_SCALE 4503599627370496.0 /* 2^52. */

unsigned long
mh_read_u16le(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8);
}

unsigned long
mh_read_u32le(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) |
		((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

void
mh_write_u16le(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
}

void
mh_write_u32le(unsigned char *bytes, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		bytes[i] = (unsigned char)(value >> (8 * i));
}

int
mh_read_f32le(const unsigned char *bytes, double *value)
{
	unsigned long bits;
	unsigned long fraction;
	int exponent;
	double result;

	bits = mh_read_u32le(bytes);
	fraction = bits & F32_FRACTION_MASK;
	exponent = (int)((bits >> F32_FRACTION_BITS) & F32_EXPONENT_MASK);

	if (exponent == F32_SPECIAL_EXPONENT) {
		return 0;
	}

	if (exponent == 0) {
		result = ldexp((double)fraction,
			1 - F32_EXPONENT_BIAS - F32_FRACTION_BITS);
	} else {
		result = ldexp((double)(fraction | F32_IMPLICIT_BIT),
			exponent - (F32_EXPONENT_BIAS + F32_FRACTION_BITS));
	}

	*value = (bits & MH_U32_SIGN_BIT) ? -result : result;

	return 1;
}

int
mh_read_f64le(const unsigned char *bytes, double *value)
{
	unsigned long high;
	int exponent;
	double fraction;
	double result;

	high = mh_read_u32le(bytes + 4);
	exponent = (int)((high >> F64_HIGH_FRACTION_BITS) & F64_EXPONENT_MASK);

	if (exponent == F64_SPECIAL_EXPONENT)
		return 0;

	fraction = (high & F64_HIGH_FRACTION_MASK) * (double)MH_U32_MODULUS +
		mh_read_u32le(bytes);
	result = exponent == 0
		? ldexp(fraction, 1 - F64_EXPONENT_BIAS - F64_FRACTION_BITS)
		: ldexp(1.0 + fraction / F64_FRACTION_SCALE,
			exponent - F64_EXPONENT_BIAS);

	if (!(result <= DBL_MAX))
		return 0;

	*value = (high & MH_U32_SIGN_BIT) ? -result : result;

	return 1;
}
