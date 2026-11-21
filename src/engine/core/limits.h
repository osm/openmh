#ifndef MH_CORE_LIMITS_H
#define MH_CORE_LIMITS_H

/*
 * Original integer widths, independent of the host's int/long widths.
 * C89 has no stdint.h. In particular, ULONG_MAX is not the original u32
 * maximum on LP64 systems. The unsigned maximum also masks wrapping words.
 */
#define MH_U32_MAX 0xffffffffUL
#define MH_I32_MAX 2147483647L
#define MH_I32_MIN (-MH_I32_MAX - 1L)
#define MH_U32_SIGN_BIT 0x80000000UL
#define MH_U16_MAX 65535
#define MH_U8_MAX 255
#define MH_U16_SIGN_BIT 32768UL
#define MH_U16_MODULUS 65536UL
#define MH_I16_MAX 32767
#define MH_I16_MIN (-MH_I16_MAX - 1)

/* 2^32 cannot be represented by unsigned long on every C89 target. */
#define MH_U32_MODULUS 4294967296.0L

#endif
