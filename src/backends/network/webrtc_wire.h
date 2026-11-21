#ifndef MH_WEBRTC_WIRE_H
#define MH_WEBRTC_WIRE_H

#include <stddef.h>

/* Private codec helpers. Callers establish two/four readable/writable bytes. */
unsigned int	mh_rtc_read16(const unsigned char *data);
unsigned long	mh_rtc_read32(const unsigned char *data);
void		mh_rtc_write16(unsigned char *data, unsigned int value);
void		mh_rtc_write32(unsigned char *data, unsigned long value);

/*
 * Reflected CRC with initial/final complement. Treat four bytes at zero_offset
 * as zero; (size_t)-1 means no excluded field. This is not authentication.
 */
unsigned long	mh_rtc_crc(const unsigned char *data, size_t size,
	size_t zero_offset, unsigned long polynomial);

#endif
