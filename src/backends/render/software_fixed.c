#include <math.h>

#include "backends/render/software_fixed.h"
#include "engine/core/limits.h"

enum {
	FRACTION_BITS = 23,
	FREXP_EXPONENT_BIAS = 126
};

#define UV_BIAS 768.0
#define UV_BIAS_BITS 0x44400000UL

/* Round to binary32 after adding 768, then subtract its bits. */
long
mh_software_texture_fixed(double value)
{
	float encoded;
	double fraction;
	unsigned long bits;
	int exponent;

	encoded = (float)(value + UV_BIAS);
	bits = 0;

	if (encoded != 0) {
		fraction = frexp(fabs((double)encoded), &exponent);

		/* frexp returns [0.5, 1), one power below binary32's 1.x. */
		bits = (unsigned long)(exponent + FREXP_EXPONENT_BIAS)
			<< FRACTION_BITS;

		bits += (unsigned long)ldexp(fraction * 2 - 1, FRACTION_BITS);

		if (encoded < 0)
			bits |= MH_U32_SIGN_BIT;
	}

	bits = (bits - UV_BIAS_BITS) & MH_U32_MAX;

	if (bits & MH_U32_SIGN_BIT)
		return -1L - (long)(MH_U32_MAX - bits);

	return (long)bits;
}
