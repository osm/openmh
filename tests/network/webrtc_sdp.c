#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/network/webrtc_sdp.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "SDP:%d: %s\n", __LINE__, #condition); \
			return 1;                                              \
		}                                                              \
	} while (0)

static const char offer_text[] =
	"v=0\r\no=- 1234567890123456789 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
	"a=group:BUNDLE data\r\na=ice-ufrag:abcd\r\na=ice-pwd:abcdefghijklmnopqrstuv\r\n"
	"a=fingerprint:sha-256 00:01:02:03:04:05:06:07:08:09:0A:0B:0C:0D:0E:0F:10:11:12:13:14:15:16:17:18:19:1A:1B:1C:1D:1E:1F\r\n"
	"a=setup:actpass\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
	"c=IN IP4 0.0.0.0\r\na=mid:data\r\na=sctp-port:5000\r\n";

static void
answer_configuration(struct mh_rtc_answer *answer, int ipv6)
{
	unsigned int i;

	memset(answer, 0, sizeof(*answer));
	strcpy(answer->ufrag, "server1234567890");
	strcpy(answer->password, "01234567890123456789012345678901");
	answer->address_size = ipv6 ? 16 : 4;
	answer->address[ipv6 ? 15 : 3] = 1;

	if (!ipv6)
		answer->address[0] = 127;

	answer->udp_port = 9;
	answer->sctp_port = 5000;
	answer->session_id = 12345;

	for (i = 0; i < 32; i++)
		answer->fingerprint[i] = (unsigned char)i;
}

static int
check_offers(void)
{
	static const char *invalid_lines[] = {"a=mid:duplicate\r\n",
		"a=sctp-port:6000\r\n", "a=ice-lite\r\n", "a=ice-ufrag:a\r\n",
		"a=ice-pwd:short\r\n", "a=setup:passive\r\n",
		"a=max-message-size:8191\r\n",
		"a=max-message-size:4294967296\r\n",
		"a=max-message-size:01\r\n", "a=mid:bad mid\r\n",
		"m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n", NULL};

	struct mh_rtc_offer offer, before;
	char text[2048];
	size_t i;

	CHECK(mh_rtc_offer_read((const unsigned char *)offer_text,
		strlen(offer_text), &offer, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(offer.ufrag, "abcd") == 0 &&
		strcmp(offer.password, "abcdefghijklmnopqrstuv") == 0);
	CHECK(strcmp(offer.mid, "data") == 0 && offer.sctp_port == 5000 &&
		offer.maximum_message == 65536UL);

	for (i = 0; i < 32; i++)
		CHECK(offer.fingerprint[i] == i);

	memcpy(&before, &offer, sizeof(before));

	/* Every prefix lacks the required complete final sctp-port line. */
	for (i = 0; i < strlen(offer_text); i++) {
		CHECK(mh_rtc_offer_read((const unsigned char *)offer_text, i,
			&offer, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&offer, &before, sizeof(offer)) == 0);
	}

	for (i = 0; invalid_lines[i] != NULL; i++) {
		strcpy(text, offer_text);
		strcat(text, invalid_lines[i]);
		CHECK(mh_rtc_offer_read((const unsigned char *)text,
			strlen(text), &offer, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&offer, &before, sizeof(offer)) == 0);
	}

	strcpy(text, offer_text);
	strcat(text,
		"a=ice-ufrag:media\r\na=ice-pwd:mediaPassword123456789012\r\na=setup:active\r\na=max-message-size:0\r\n");
	CHECK(mh_rtc_offer_read((const unsigned char *)text, strlen(text),
		&offer, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(offer.ufrag, "media") == 0 &&
		strcmp(offer.password, "mediaPassword123456789012") == 0 &&
		offer.maximum_message == 0);
	strcat(text, "a=ice-ufrag:again\r\n");
	CHECK(mh_rtc_offer_read((const unsigned char *)text, strlen(text),
		&offer, NULL) != MH_ERROR_NONE);
	strcpy(text, offer_text);
	text[100] = 0;
	CHECK(mh_rtc_offer_read((const unsigned char *)text, strlen(offer_text),
		&offer, NULL) != MH_ERROR_NONE);

	return 0;
}

static int
check_answers(void)
{
	struct mh_rtc_offer offer;
	struct mh_rtc_answer answer;
	char output[2048], before[2048], expected[2048];
	size_t size, written, capacity;
	int ipv6;

	CHECK(mh_rtc_offer_read((const unsigned char *)offer_text,
		strlen(offer_text), &offer, NULL) == MH_ERROR_NONE);

	for (ipv6 = 0; ipv6 <= 1; ipv6++) {
		answer_configuration(&answer, ipv6);
		CHECK(mh_rtc_answer_write(&offer, &answer, expected,
			sizeof(expected), &size, NULL) == MH_ERROR_NONE);
		CHECK(size == strlen(expected));
		CHECK(strstr(expected, "a=ice-lite\r\n") != NULL &&
			strstr(expected, "a=setup:passive\r\n") != NULL);
		CHECK(strstr(expected,
			"a=sctp-port:5000\r\na=max-message-size:8192\r\n") !=
			NULL);
		CHECK(strstr(expected,
			ipv6 ? "c=IN IP6 0:0:0:0:0:0:0:1\r\n"
				: "c=IN IP4 127.0.0.1\r\n") != NULL);
		memset(output, '#', sizeof(output));
		memcpy(before, output, sizeof(before));

		for (capacity = 0; capacity <= size; capacity++) {
			written = 42;
			CHECK(mh_rtc_answer_write(&offer, &answer, output,
				capacity, &written,
				NULL) == MH_ERROR_RANGE);
			CHECK(written == 42 &&
				memcmp(output, before, sizeof(output)) == 0);
		}

		CHECK(mh_rtc_answer_write(&offer, &answer, output, size + 1,
			&written, NULL) == MH_ERROR_NONE);
		CHECK(written == size &&
			memcmp(output, expected, size + 1) == 0 &&
			output[size + 1] == '#');
		answer.password[0] = '\n';
		memcpy(before, output, sizeof(before));
		CHECK(mh_rtc_answer_write(&offer, &answer, output,
			sizeof(output), &written,
			NULL) == MH_ERROR_INVALID);
		CHECK(memcmp(output, before, sizeof(output)) == 0);
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	unsigned char input[MH_RTC_SDP_BYTES + 1];
	char output[2048];
	struct mh_rtc_offer offer;
	struct mh_rtc_answer answer;
	size_t size;

	if (argc == 2 &&
		(strcmp(argv[1], "--answer4") == 0 ||
			strcmp(argv[1], "--answer6") == 0)) {
		size = fread(input, 1, sizeof(input), stdin);
		CHECK(!ferror(stdin));
		CHECK(mh_rtc_offer_read(input, size, &offer, NULL) ==
			MH_ERROR_NONE);
		answer_configuration(
			&answer, strcmp(argv[1], "--answer6") == 0);
		CHECK(mh_rtc_answer_write(&offer, &answer, output,
			sizeof(output), &size, NULL) == MH_ERROR_NONE);
		CHECK(fwrite(output, 1, size, stdout) == size);
		return 0;
	}

	CHECK(argc == 1);
	CHECK(check_offers() == 0);
	CHECK(check_answers() == 0);

	return 0;
}
