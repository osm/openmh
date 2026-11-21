#include <stdlib.h>
#include <string.h>

#include "backends/network/webrtc_signaling.h"

int	LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

int
LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	struct mh_signal_request request, before;
	unsigned char response[MH_SIGNAL_RESPONSE_BYTES];
	size_t written;
	int result;

	memset(&request, 0xa5, sizeof(request));
	memcpy(&before, &request, sizeof(before));
	result = mh_signal_request_read(data, size, &request);

	if (result == MH_SIGNAL_READY) {
		if (request.body_offset > size ||
			request.body_size != size - request.body_offset ||
			request.body_size > MH_RTC_SDP_BYTES ||
			(request.origin[0] != 0 &&
				!mh_signal_origin_valid(request.origin)))
			abort();

		if (mh_signal_response_write(request.method == MH_SIGNAL_POST
				? MH_SIGNAL_OK
				: MH_SIGNAL_NO_CONTENT,
			request.origin[0] ? request.origin : NULL,
			data + request.body_offset, request.body_size,
			response, sizeof(response), &written,
			NULL) != MH_ERROR_NONE ||
			written > sizeof(response))
			abort();
	} else if (memcmp(&request, &before, sizeof(request)) != 0)
		abort();

	return 0;
}
