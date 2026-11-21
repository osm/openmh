#include <stdio.h>
#include <string.h>

#include "backends/network/webrtc_signaling.h"
#include "engine/core/text.h"

static const char *
status_text(int status)
{
	switch (status) {
	case MH_SIGNAL_OK:
		return "200 OK";
	case MH_SIGNAL_NO_CONTENT:
		return "204 No Content";
	case MH_SIGNAL_BAD_REQUEST:
		return "400 Bad Request";
	case MH_SIGNAL_FORBIDDEN:
		return "403 Forbidden";
	case MH_SIGNAL_NOT_FOUND:
		return "404 Not Found";
	case MH_SIGNAL_TOO_LARGE:
		return "413 Content Too Large";
	case MH_SIGNAL_UNSUPPORTED_MEDIA:
		return "415 Unsupported Media Type";
	case MH_SIGNAL_EXPECTATION_FAILED:
		return "417 Expectation Failed";
	case MH_SIGNAL_HEADER_TOO_LARGE:
		return "431 Request Header Fields Too Large";
	case MH_SIGNAL_INTERNAL_ERROR:
		return "500 Internal Server Error";
	case MH_SIGNAL_UNAVAILABLE:
		return "503 Service Unavailable";
	default:
		return NULL;
	}
}

enum mh_error_code
mh_signal_response_write(int status, const char *origin,
	const unsigned char *body, size_t size, unsigned char *output,
	size_t capacity, size_t *written, struct mh_error *error_out)
{
	char header[MH_SIGNAL_RESPONSE_HEADER_BYTES];
	char length[32];
	const char *reason;
	size_t header_size;

	reason = status_text(status);

	if (reason == NULL || output == NULL || written == NULL ||
		size > MH_RTC_SDP_BYTES || (size != 0 && body == NULL) ||
		(status != MH_SIGNAL_OK && size != 0) ||
		(origin != NULL && !mh_signal_origin_valid(origin)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid signaling response");

	header[0] = 0;

	if (mh_text_append(header, sizeof(header), "HTTP/1.1 ", error_out) !=
			MH_ERROR_NONE ||
		mh_text_append(header, sizeof(header), reason, error_out) !=
			MH_ERROR_NONE ||
		mh_text_append(header, sizeof(header),
			"\r\nConnection: close\r\nCache-Control: no-store\r\n",
			error_out) != MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	if (origin != NULL) {
		if (mh_text_append(header, sizeof(header),
			"Access-Control-Allow-Origin: ", error_out) !=
				MH_ERROR_NONE ||
			mh_text_append(header, sizeof(header), origin,
				error_out) != MH_ERROR_NONE ||
			mh_text_append(header, sizeof(header),
				"\r\nVary: Origin\r\n",
				error_out) != MH_ERROR_NONE)
			return MH_ERROR_RANGE;
	}

	if (status == MH_SIGNAL_NO_CONTENT) {
		if (mh_text_append(header, sizeof(header),
			"Access-Control-Allow-Methods: POST\r\nAccess-Control-Allow-Headers: Content-Type\r\n",
			error_out) != MH_ERROR_NONE)
			return MH_ERROR_RANGE;
	} else {
		/*
		 * size is bounded by the SDP limit before decimal formatting.
		 */
		sprintf(length, "%lu", (unsigned long)size);

		if (mh_text_append(header, sizeof(header),
			"Content-Length: ", error_out) != MH_ERROR_NONE ||
			mh_text_append(header, sizeof(header), length,
				error_out) != MH_ERROR_NONE ||
			mh_text_append(header, sizeof(header), "\r\n",
				error_out) != MH_ERROR_NONE)
			return MH_ERROR_RANGE;
	}

	if (status == MH_SIGNAL_OK &&
		mh_text_append(header, sizeof(header),
			"Content-Type: application/sdp\r\n",
			error_out) != MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	if (mh_text_append(header, sizeof(header), "\r\n", error_out) !=
		MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	header_size = strlen(header);

	if (header_size > capacity || size > capacity - header_size)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"signaling response buffer is too small");

	/* Preserve body/output overlap by moving the body before its header. */
	if (size != 0)
		memmove(output + header_size, body, size);

	memcpy(output, header, header_size);
	*written = header_size + size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
