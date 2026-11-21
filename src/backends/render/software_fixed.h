#ifndef MH_SOFTWARE_FIXED_H
#define MH_SOFTWARE_FIXED_H

/* Fourteen fractional texture-coordinate bits. */
#define MH_SOFTWARE_TEXTURE_SCALE 16384L

/* Original binary32 bias conversion, interpreted as a signed 32-bit word. */
long	mh_software_texture_fixed(double value);

#endif
