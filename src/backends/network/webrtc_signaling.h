#ifndef MH_WEBRTC_SIGNALING_H
#define MH_WEBRTC_SIGNALING_H

#include "backends/network/webrtc_sdp.h"

enum {
	MH_SIGNAL_HEADER_BYTES = 8192,
	MH_SIGNAL_ORIGIN_BYTES = 1024,
	MH_SIGNAL_ADDRESS_BYTES = 64,
	MH_SIGNAL_REQUEST_BYTES = MH_SIGNAL_HEADER_BYTES + MH_RTC_SDP_BYTES,
	MH_SIGNAL_RESPONSE_HEADER_BYTES = 1536,
	MH_SIGNAL_RESPONSE_BYTES =
		MH_SIGNAL_RESPONSE_HEADER_BYTES + MH_RTC_SDP_BYTES,
	MH_SIGNAL_NEED_MORE = 0,
	MH_SIGNAL_READY = 1,
	MH_SIGNAL_OK = 200,
	MH_SIGNAL_NO_CONTENT = 204,
	MH_SIGNAL_BAD_REQUEST = 400,
	MH_SIGNAL_FORBIDDEN = 403,
	MH_SIGNAL_NOT_FOUND = 404,
	MH_SIGNAL_TOO_LARGE = 413,
	MH_SIGNAL_UNSUPPORTED_MEDIA = 415,
	MH_SIGNAL_EXPECTATION_FAILED = 417,
	MH_SIGNAL_HEADER_TOO_LARGE = 431,
	MH_SIGNAL_INTERNAL_ERROR = 500,
	MH_SIGNAL_UNAVAILABLE = 503
};

enum mh_signal_method {
	MH_SIGNAL_POST,
	MH_SIGNAL_OPTIONS
};

struct mh_signal_request {
	enum mh_signal_method	method;
	size_t			body_offset, body_size;
	char			origin[MH_SIGNAL_ORIGIN_BYTES];
	char			real_ip[MH_SIGNAL_ADDRESS_BYTES];
};

/*
 * Narrow / endpoint framing: HTTP/1.0 or 1.1, one Host, Content-Length
 * for SDP POST, and bodyless OPTIONS. No transfer/content encoding, folded
 * fields, duplicate framing fields or pipelining. Every response closes TCP.
 * Return NEED_MORE, READY or an HTTP failure status. Only READY writes output.
 * The body remains borrowed from input; all header fields are copied.
 */
int	mh_signal_request_read(const unsigned char *data, size_t size,
	struct mh_signal_request *output);
int	mh_signal_origin_valid(const char *origin);

/*
 * Exact allowed origin or NULL; rejects header injection and preserves output
 * on failure. Successful SDP answers have Content-Type application/sdp.
 */
enum mh_error_code	mh_signal_response_write(int status, const char *origin,
	const unsigned char *body, size_t size, unsigned char *output,
	size_t capacity, size_t *written, struct mh_error *error_out);

#endif
