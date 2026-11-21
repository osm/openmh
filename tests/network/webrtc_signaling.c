#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/network/webrtc_signaling.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "signaling:%d: %s\n", __LINE__,        \
				#condition);                                   \
			exit(1);                                               \
		}                                                              \
	} while (0)

static const char post[] =
	"POST / HTTP/1.1\r\nHost: game.invalid\r\n"
	"Origin: https://game.invalid\r\nContent-Type: application/sdp\r\n"
	"Content-Length: 5\r\nX-Real-IP: 2001:db8::1\r\n\r\noffer";

static void
reject(const char *extra, int expected)
{
	char input[MH_SIGNAL_REQUEST_BYTES];
	struct mh_signal_request request, before;
	size_t length;

	length = strlen(extra);
	CHECK(length + sizeof(post) < sizeof(input));
	memcpy(input, post, sizeof("POST / HTTP/1.1\r\n") - 1);
	memcpy(input + sizeof("POST / HTTP/1.1\r\n") - 1, extra, length);
	strcpy(input + sizeof("POST / HTTP/1.1\r\n") - 1 + length,
		post + sizeof("POST / HTTP/1.1\r\n") - 1);
	memset(&request, 0x5a, sizeof(request));
	memcpy(&before, &request, sizeof(before));
	CHECK(mh_signal_request_read((const unsigned char *)input,
		strlen(input), &request) == expected);
	CHECK(memcmp(&request, &before, sizeof(request)) == 0);
}

int
main(void)
{
	static const char preflight[] =
		"OPTIONS / HTTP/1.1\r\nHost: game.invalid\r\n"
		"Origin: https://game.invalid\r\nAccess-Control-Request-Method: POST\r\n"
		"Access-Control-Request-Headers: content-type\r\n\r\n";
	struct mh_signal_request request, before;
	unsigned char input[MH_SIGNAL_REQUEST_BYTES + 1];
	unsigned char output[MH_SIGNAL_RESPONSE_BYTES + 1],
		saved[sizeof(output)];
	size_t i, size, header_size;

	memset(&request, 0xa5, sizeof(request));
	memcpy(&before, &request, sizeof(before));

	for (i = 0; i < sizeof(post) - 1; i++) {
		CHECK(mh_signal_request_read((const unsigned char *)post, i,
			&request) == MH_SIGNAL_NEED_MORE);
		CHECK(memcmp(&request, &before, sizeof(request)) == 0);
	}

	CHECK(mh_signal_request_read((const unsigned char *)post,
		sizeof(post) - 1, &request) == MH_SIGNAL_READY);
	CHECK(request.method == MH_SIGNAL_POST && request.body_size == 5);
	CHECK(request.body_offset + request.body_size == sizeof(post) - 1);
	CHECK(strcmp(request.origin, "https://game.invalid") == 0 &&
		strcmp(request.real_ip, "2001:db8::1") == 0);
	CHECK(mh_signal_request_read((const unsigned char *)preflight,
		sizeof(preflight) - 1, &request) == MH_SIGNAL_READY);
	CHECK(request.method == MH_SIGNAL_OPTIONS && request.body_size == 0);
	reject("Content-Length: 5\r\n", MH_SIGNAL_BAD_REQUEST);
	reject("Content-Length: 5, 5\r\n", MH_SIGNAL_BAD_REQUEST);
	reject("Transfer-Encoding: chunked\r\n", MH_SIGNAL_BAD_REQUEST);
	reject("Content-Encoding: gzip\r\n", MH_SIGNAL_BAD_REQUEST);
	reject("Host: second.invalid\r\n", MH_SIGNAL_BAD_REQUEST);
	reject("Content-Length: 9999999999999999999999999999999999\r\n",
		MH_SIGNAL_TOO_LARGE);
	reject("Origin: https://other.invalid\r\n", MH_SIGNAL_BAD_REQUEST);
	reject("X-Real-IP: 192.0.2.1\r\n", MH_SIGNAL_BAD_REQUEST);
	reject("Content-Type: application/json\r\n",
		MH_SIGNAL_UNSUPPORTED_MEDIA);
	reject("Expect: 100-continue\r\n", MH_SIGNAL_EXPECTATION_FAILED);
	reject(" bad: fold\r\n", MH_SIGNAL_BAD_REQUEST);
	reject("Fold: one\r\n two\r\n", MH_SIGNAL_BAD_REQUEST);
	reject("Bad : space\r\n", MH_SIGNAL_BAD_REQUEST);
	reject("Bad: lone\n", MH_SIGNAL_BAD_REQUEST);
	reject("Bad: lone\r", MH_SIGNAL_BAD_REQUEST);
	memcpy(input, post, sizeof(post));
	CHECK(mh_signal_request_read(input, sizeof(post), &request) ==
		MH_SIGNAL_BAD_REQUEST);
	input[0] = 0;
	CHECK(mh_signal_request_read(input, sizeof(post) - 1, &request) ==
		MH_SIGNAL_BAD_REQUEST);
	memset(input, 'a', MH_SIGNAL_HEADER_BYTES);
	CHECK(mh_signal_request_read(input, MH_SIGNAL_HEADER_BYTES, &request) ==
		MH_SIGNAL_HEADER_TOO_LARGE);
	CHECK(mh_signal_origin_valid("https://[2001:db8::1]:8443"));
	CHECK(!mh_signal_origin_valid("https://game.invalid/path"));
	CHECK(!mh_signal_origin_valid("https://game.invalid\r\nInjected: yes"));
	CHECK(!mh_signal_origin_valid("https://user@game.invalid"));
	CHECK(!mh_signal_origin_valid("null") && !mh_signal_origin_valid("*"));
	memset(output, 0x5a, sizeof(output));
	memcpy(saved, output, sizeof(saved));
	size = 99;
	CHECK(mh_signal_response_write(MH_SIGNAL_OK, "https://game.invalid",
		(const unsigned char *)"answer", 6, output, 1, &size,
		NULL) == MH_ERROR_RANGE);
	CHECK(size == 99 && memcmp(output, saved, sizeof(saved)) == 0);
	CHECK(mh_signal_response_write(MH_SIGNAL_OK,
		"https://game.invalid\r\nInjected: yes", NULL, 0, output,
		sizeof(output), &size, NULL) == MH_ERROR_INVALID);
	CHECK(size == 99 && memcmp(output, saved, sizeof(saved)) == 0);
	CHECK(mh_signal_response_write(MH_SIGNAL_OK, "https://game.invalid",
		(const unsigned char *)"answer", 6, output,
		sizeof(output), &size, NULL) == MH_ERROR_NONE);
	output[size] = 0;
	CHECK(strstr((const char *)output, "Content-Length: 6\r\n") != NULL &&
		strstr((const char *)output,
			"Content-Type: application/sdp\r\n") != NULL &&
		strstr((const char *)output, "\r\n\r\nanswer") != NULL);
	CHECK(mh_signal_response_write(MH_SIGNAL_NO_CONTENT,
		"https://game.invalid", NULL, 0, output, sizeof(output),
		&size, NULL) == MH_ERROR_NONE);
	output[size] = 0;
	CHECK(strstr((const char *)output, "Content-Length:") == NULL &&
		strstr((const char *)output,
			"Access-Control-Allow-Methods: POST\r\n") != NULL);
	header_size = (size_t)sprintf((char *)input,
		"POST / HTTP/1.1\r\nHost: game.invalid\r\nContent-Type: application/sdp\r\nContent-Length: %u\r\n\r\n",
		MH_RTC_SDP_BYTES);
	memset(input + header_size, 'x', MH_RTC_SDP_BYTES);
	CHECK(mh_signal_request_read(input, header_size + MH_RTC_SDP_BYTES,
		&request) == MH_SIGNAL_READY);
	CHECK(request.body_size == MH_RTC_SDP_BYTES);
	memset(output, 'x', MH_RTC_SDP_BYTES);
	CHECK(mh_signal_response_write(MH_SIGNAL_OK, NULL, output,
		MH_RTC_SDP_BYTES, output, sizeof(output), &size,
		NULL) == MH_ERROR_NONE);
	CHECK(size > MH_RTC_SDP_BYTES && output[size - 1] == 'x');

	return 0;
}
