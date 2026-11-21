#ifndef MH_LIGHT_PRODUCT_H
#define MH_LIGHT_PRODUCT_H

#include <float.h>

/*
 * A binary32 product needs at most 48 significant bits. Binary64 holds it
 * exactly, including subnormals, without Wasm's software extended arithmetic.
 * Use this type only for individual float-by-float products; sums and compound
 * expressions may still need extended precision. Retain the original type on
 * targets without a sufficiently wide double.
 */
#if FLT_RADIX == 2 && DBL_MANT_DIG >= 2 * FLT_MANT_DIG &&                      \
	DBL_MAX_EXP >= 2 * FLT_MAX_EXP &&                                      \
	DBL_MIN_EXP <= 2 * (FLT_MIN_EXP - FLT_MANT_DIG)
typedef double mh_light_product;
#else
typedef long double mh_light_product;
#endif

#endif
