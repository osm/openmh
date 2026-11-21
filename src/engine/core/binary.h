#ifndef MH_BINARY_H
#define MH_BINARY_H

/*
 * Callers must first establish that the input contains 2, 4 or 8 bytes.
 * unsigned long holds all 32-bit file values on C89 implementations.
 * Its native width need not match the file format.
 */
unsigned long	mh_read_u16le(const unsigned char *bytes);
unsigned long	mh_read_u32le(const unsigned char *bytes);

/* Write the low 16 bits; callers must provide two writable bytes. */
void	mh_write_u16le(unsigned char *bytes, unsigned long value);

/* Write the low 32 bits; callers must provide four writable bytes. */
void	mh_write_u32le(unsigned char *bytes, unsigned long value);

/* Decode finite IEEE-754 binary32 to a native double. Return 0 for NaN/Inf. */
int	mh_read_f32le(const unsigned char *bytes, double *value);

/* Decode finite IEEE-754 binary64; output unchanged on NaN/Inf/overflow. */
int	mh_read_f64le(const unsigned char *bytes, double *value);

#endif
