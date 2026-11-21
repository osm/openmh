#include "backends/network/webrtc_wire.h"

unsigned int
mh_rtc_read16(const unsigned char *data)
{
	return (unsigned int)data[0] * 256U + data[1];
}

unsigned long
mh_rtc_read32(const unsigned char *data)
{
	return ((unsigned long)data[0] << 24) | ((unsigned long)data[1] << 16) |
		((unsigned long)data[2] << 8) | data[3];
}

void
mh_rtc_write16(unsigned char *data, unsigned int value)
{
	data[0] = (unsigned char)(value >> 8);
	data[1] = (unsigned char)value;
}

void
mh_rtc_write32(unsigned char *data, unsigned long value)
{
	data[0] = (unsigned char)(value >> 24);
	data[1] = (unsigned char)(value >> 16);
	data[2] = (unsigned char)(value >> 8);
	data[3] = (unsigned char)value;
}

unsigned long
mh_rtc_crc(const unsigned char *data, size_t size, size_t zero_offset,
	unsigned long polynomial)
{
	unsigned long crc;
	size_t i;
	unsigned int bit;

	crc = 0xffffffffUL;

	for (i = 0; i < size; i++) {
		if (i < zero_offset || i - zero_offset >= 4)
			crc ^= data[i];

		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1) ? polynomial : 0);
	}

	return crc ^ 0xffffffffUL;
}
