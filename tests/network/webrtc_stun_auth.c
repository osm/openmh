#include <stdio.h>
#include <string.h>

#include "backends/network/webrtc_stun_auth.h"
#include "backends/network/webrtc_wire.h"
#include "stun_vectors.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "STUN authentication:%d: %s\n",        \
				__LINE__, #condition);                         \
			return 1;                                              \
		}                                                              \
	} while (0)

static const char password[] = "VOkJxbRl1RmTxUk/WvJxBt";

/* Independently generated with Python hmac/hashlib/zlib and the RFC key/ID. */
static const unsigned char reply4[] =
	"\x01\x01\x00\x2c\x21\x12\xa4\x42\xb7\xe7\xa7\x01"
	"\xbc\x34\xd6\x86\xfa\x87\xdf\xae\x00\x20\x00\x08"
	"\x00\x01\xa1\x47\xe1\x12\xa6\x43\x00\x08\x00\x14"
	"\x74\xc9\x37\x1e\xbf\x31\x48\x54\x85\x18\x69\x9c"
	"\x3e\x31\x74\xc2\x0d\xd9\xe6\x8a\x80\x28\x00\x04"
	"\xfa\xe4\x04\x3a";
static const unsigned char reply6[] =
	"\x01\x01\x00\x38\x21\x12\xa4\x42\xb7\xe7\xa7\x01"
	"\xbc\x34\xd6\x86\xfa\x87\xdf\xae\x00\x20\x00\x14"
	"\x00\x02\xa1\x47\x01\x13\xa9\xfa\xa5\xd3\xf1\x79"
	"\xbc\x25\xf4\xb5\xbe\xd2\xb9\xd9\x00\x08\x00\x14"
	"\xee\x33\xa0\x55\x53\x19\xee\xc1\x0a\xd5\xfb\xfd"
	"\xf8\x73\x3d\x19\x6e\x55\x2b\x3c\x80\x28\x00\x04"
	"\x5d\xed\x71\x86";

static int
replies(void)
{
	struct mh_stun_message message, response;
	unsigned char address[16], decoded[16], output[80], before[80];
	size_t address_size, size, capacity, length;
	unsigned int port;
	const unsigned char *expected;

	CHECK(mh_webrtc_stun_check(request, sizeof(request) - 1, "evtj:h6vY",
		password, &message, NULL) == MH_ERROR_NONE);

	for (address_size = 4; address_size <= 16; address_size += 12) {
		if (address_size == 4) {
			memcpy(address, "\xc0\x00\x02\x01", 4);
			expected = reply4;
			length = sizeof(reply4) - 1;
		} else {
			memcpy(address,
				"\x20\x01\x0d\xb8\x12\x34\x56\x78\x00\x11\x22\x33\x44\x55\x66\x77",
				16);
			expected = reply6;
			length = sizeof(reply6) - 1;
		}

		memset(output, '#', sizeof(output));
		memcpy(before, output, sizeof(before));

		for (capacity = 0; capacity < length; capacity++) {
			size = 123;
			CHECK(mh_webrtc_stun_reply(&message, address,
				address_size, 32853, password, output,
				capacity, &size, NULL) == MH_ERROR_RANGE);
			CHECK(size == 123 &&
				memcmp(output, before, sizeof(output)) == 0);
		}

		CHECK(mh_webrtc_stun_reply(&message, address, address_size,
			32853, NULL, output, sizeof(output), &size,
			NULL) == MH_ERROR_INVALID);
		CHECK(size == 123 &&
			memcmp(output, before, sizeof(output)) == 0);
		CHECK(mh_webrtc_stun_reply(&message, address, address_size,
			32853, password, output, length, &size,
			NULL) == MH_ERROR_NONE);
		CHECK(size == length && memcmp(output, expected, size) == 0);
		CHECK(output[length] == '#');
		CHECK(mh_stun_read(output, size, &response, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_stun_mapped(&response, decoded, &size, &port, NULL) ==
			MH_ERROR_NONE);
		CHECK(size == address_size && port == 32853 &&
			memcmp(decoded, address, size) == 0);
	}

	return 0;
}

static int
authentication(void)
{
	struct mh_stun_message message, before;
	unsigned char changed[sizeof(request)];
	unsigned long fingerprint;

	CHECK(mh_webrtc_stun_check(request, sizeof(request) - 1, "evtj:h6vY",
		password, &message, NULL) == MH_ERROR_NONE);
	memcpy(&before, &message, sizeof(before));
	CHECK(mh_webrtc_stun_check(request, sizeof(request) - 1, "other:peer",
		password, &message, NULL) == MH_ERROR_PERMISSION);
	CHECK(memcmp(&message, &before, sizeof(message)) == 0);
	CHECK(mh_webrtc_stun_check(request, sizeof(request) - 1, "evtj:h6vY",
		"wrong password", &message, NULL) == MH_ERROR_PERMISSION);
	CHECK(memcmp(&message, &before, sizeof(message)) == 0);
	CHECK(mh_webrtc_stun_check(request, sizeof(request) - 1, "evtj:h6vY",
		NULL, &message, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&message, &before, sizeof(message)) == 0);

	/*
	 * A recomputed CRC cannot authenticate changed connectivity metadata.
	 */
	memcpy(changed, request, sizeof(request));
	changed[44] ^= 1;
	fingerprint = mh_rtc_crc(changed, 100, (size_t)-1, 0xedb88320UL) ^
		0x5354554eUL;
	mh_rtc_write32(changed + 104, fingerprint);
	CHECK(mh_webrtc_stun_check(changed, sizeof(request) - 1, "evtj:h6vY",
		password, &message, NULL) == MH_ERROR_PERMISSION);
	CHECK(memcmp(&message, &before, sizeof(message)) == 0);

	memcpy(changed, request, sizeof(request));
	mh_rtc_write16(changed + 2, 80);
	CHECK(mh_webrtc_stun_check(changed, 100, "evtj:h6vY", password,
		&message, NULL) == MH_ERROR_PERMISSION);
	CHECK(memcmp(&message, &before, sizeof(message)) == 0);
	CHECK(mh_webrtc_stun_check(response4, sizeof(response4) - 1,
		"evtj:h6vY", password, &message,
		NULL) == MH_ERROR_PERMISSION);
	CHECK(mh_webrtc_stun_check(response6, sizeof(response6) - 1,
		"evtj:h6vY", password, &message,
		NULL) == MH_ERROR_PERMISSION);

	return 0;
}

int
main(void)
{
	CHECK(authentication() == 0);
	CHECK(replies() == 0);

	return 0;
}
