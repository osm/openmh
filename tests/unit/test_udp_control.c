#include <stdio.h>
#include <string.h>

#include "backends/network/udp_control.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
fixed_messages(void)
{
	static const unsigned char hello[] = {
		1, 0, 8, 0, 0x66, 0x66, 0xa6, 0x3f};

	static const unsigned char query[] = {4, 0, 4, 0};
	static const unsigned char reject[] = {3, 0, 8, 0, 'N', 'o', '!', 0};
	struct mh_udp_control message, saved;
	unsigned char data[32];
	size_t size;

	CHECK(mh_udp_control_decode(hello, sizeof(hello), &message, NULL) ==
		MH_ERROR_NONE);
	CHECK(message.type == MH_UDP_HELLO &&
		message.version_bits == MH_UDP_VERSION_BITS);
	CHECK(mh_udp_control_encode(&message, data, sizeof(data), &size,
		NULL) == MH_ERROR_NONE);
	CHECK(size == sizeof(hello) && memcmp(data, hello, size) == 0);
	CHECK(mh_udp_control_decode(query, sizeof(query), &message, NULL) ==
		MH_ERROR_NONE);
	CHECK(message.type == MH_UDP_QUERY);
	CHECK(mh_udp_control_encode(&message, data, sizeof(data), &size,
		NULL) == MH_ERROR_NONE);
	CHECK(size == sizeof(query) && memcmp(data, query, size) == 0);
	CHECK(mh_udp_control_decode(reject, sizeof(reject), &message, NULL) ==
		MH_ERROR_NONE);
	CHECK(message.type == MH_UDP_REJECT &&
		strcmp(message.rejection, "No!") == 0);
	CHECK(mh_udp_control_encode(&message, data, sizeof(data), &size,
		NULL) == MH_ERROR_NONE);
	CHECK(size == sizeof(reject) && memcmp(data, reject, size) == 0);
	saved = message;
	CHECK(mh_udp_control_decode(reject, sizeof(reject) - 1, &message,
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&message, &saved, sizeof(message)) == 0);
	data[7] = 'x';
	CHECK(mh_udp_control_decode(data, 8, &message, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&message, &saved, sizeof(message)) == 0);
}

static void
descriptions(void)
{
	struct mh_udp_control message, decoded;
	unsigned char description[1103], data[1123], saved[1123];
	size_t size, i;

	for (i = 0; i < sizeof(description); i++)
		description[i] = (unsigned char)i;

	memset(&message, 0, sizeof(message));
	message.type = MH_UDP_DESCRIPTION;
	strcpy(message.name, "Motorhead");
	message.description = description;
	message.description_size = sizeof(description);
	CHECK(mh_udp_control_encode(&message, data, sizeof(data), &size,
		NULL) == MH_ERROR_NONE);
	CHECK(size == sizeof(data) && data[0] == 5 && data[2] == 0x63 &&
		data[3] == 4);
	CHECK(memcmp(data + 20, description, sizeof(description)) == 0);
	CHECK(mh_udp_control_decode(data, size, &decoded, NULL) ==
		MH_ERROR_NONE);
	CHECK(decoded.description == data + 20 &&
		decoded.description_size == sizeof(description));
	CHECK(strcmp(decoded.name, "Motorhead") == 0);
	memcpy(saved, data, sizeof(data));
	size = 123;
	CHECK(mh_udp_control_encode(&message, data, sizeof(data) - 1, &size,
		NULL) == MH_ERROR_RANGE);
	CHECK(size == 123 && memcmp(saved, data, sizeof(data)) == 0);
	message.type = MH_UDP_ACCEPT;
	CHECK(mh_udp_control_encode(&message, data, sizeof(data), &size,
		NULL) == MH_ERROR_NONE);
	CHECK(size == 20 && data[0] == 2 && data[2] == 20);

	for (i = 4 + strlen(message.name); i < 20; i++)
		CHECK(data[i] == 0);

	memset(data + 4, 'x', 16);
	CHECK(mh_udp_control_decode(data, size, &decoded, NULL) ==
		MH_ERROR_INVALID);
}

int
main(void)
{
	fixed_messages();
	descriptions();

	return failures != 0;
}
