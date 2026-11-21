#include <string.h>

#include "backends/network/webrtc_signaling.h"
#include "engine/core/text.h"

#define SIGNAL_HEADER_FIELDS 64

enum {
	LINE_END_BYTES = sizeof("\r\n") - 1,
	HEAD_END_BYTES = 2 * LINE_END_BYTES
};

struct field {
	const unsigned char	*data;
	size_t			size;
};

static int
equal(struct field field, const char *text)
{
	return field.size == strlen(text) &&
		mh_ascii_equal_n((const char *)field.data, text, field.size);
}

static int
copy_field(struct field field, char *output, size_t capacity)
{
	if (field.size >= capacity)
		return 0;

	memcpy(output, field.data, field.size);
	output[field.size] = 0;

	return 1;
}

static int
name_byte(unsigned char value)
{
	return (value >= 'a' && value <= 'z') ||
		(value >= 'A' && value <= 'Z') ||
		(value >= '0' && value <= '9') ||
		(value != 0 && strchr("!#$%&'*+-.^_`|~", value) != NULL);
}

static int
content_length(struct field field, size_t *output)
{
	size_t i, size;
	unsigned int digit;

	if (field.size == 0)
		return MH_SIGNAL_BAD_REQUEST;

	size = 0;

	for (i = 0; i < field.size; i++) {
		if (field.data[i] < '0' || field.data[i] > '9')
			return MH_SIGNAL_BAD_REQUEST;

		digit = field.data[i] - '0';

		if (size > (MH_RTC_SDP_BYTES - digit) / 10)
			return MH_SIGNAL_TOO_LARGE;

		size = size * 10 + digit;
	}

	*output = size;

	return MH_SIGNAL_READY;
}

int
mh_signal_origin_valid(const char *origin)
{
	size_t i, start;
	unsigned char value;

	if (origin == NULL)
		return 0;

	if (strncmp(origin, "https://", 8) == 0)
		start = 8;
	else if (strncmp(origin, "http://", 7) == 0)
		start = 7;
	else
		return 0;

	for (i = start; i < MH_SIGNAL_ORIGIN_BYTES && origin[i] != 0; i++) {
		value = (unsigned char)origin[i];

		if (!((value >= 'a' && value <= 'z') ||
			(value >= 'A' && value <= 'Z') ||
			(value >= '0' && value <= '9') || value == '-' ||
			value == '.' || value == ':' || value == '[' ||
			value == ']'))
			return 0;
	}

	return i > start && i < MH_SIGNAL_ORIGIN_BYTES;
}

int
mh_signal_request_read(const unsigned char *data, size_t size,
	struct mh_signal_request *output)
{
	struct mh_signal_request request;
	struct field name, value;
	size_t i, end, line, colon, header_size;
	unsigned int fields;
	int host, length, type, origin, real_ip, method, headers, status;

	if (data == NULL || output == NULL)
		return MH_SIGNAL_BAD_REQUEST;

	header_size = 0;

	for (i = 0; i < size && i < MH_SIGNAL_HEADER_BYTES; i++) {
		if (data[i] == 0 || data[i] > 126 ||
			(data[i] < 32 && data[i] != '\r' && data[i] != '\n' &&
				data[i] != '\t'))
			return MH_SIGNAL_BAD_REQUEST;

		if ((data[i] == '\n' && (i == 0 || data[i - 1] != '\r')) ||
			(data[i] == '\r' && i + 1 < size &&
				data[i + 1] != '\n'))
			return MH_SIGNAL_BAD_REQUEST;

		if (i + 1 >= HEAD_END_BYTES &&
			memcmp(data + i + 1 - HEAD_END_BYTES, "\r\n\r\n",
				HEAD_END_BYTES) == 0) {
			header_size = i + 1;
			break;
		}
	}

	if (header_size == 0)
		return size >= MH_SIGNAL_HEADER_BYTES
			? MH_SIGNAL_HEADER_TOO_LARGE
			: MH_SIGNAL_NEED_MORE;

	memset(&request, 0, sizeof(request));
	request.body_offset = header_size;

	for (end = 0; end < header_size && data[end] != '\r'; end++)
		;

	if (end < sizeof(" HTTP/1.1") - 1 ||
		(memcmp(data + end - (sizeof(" HTTP/1.1") - 1), " HTTP/1.1",
			sizeof(" HTTP/1.1") - 1) != 0 &&
			memcmp(data + end - (sizeof(" HTTP/1.0") - 1),
				" HTTP/1.0", sizeof(" HTTP/1.0") - 1) != 0))
		return MH_SIGNAL_BAD_REQUEST;

	if (end == sizeof("POST / HTTP/1.1") - 1 &&
		memcmp(data, "POST /", sizeof("POST /") - 1) == 0)
		request.method = MH_SIGNAL_POST;
	else if (end == sizeof("OPTIONS / HTTP/1.1") - 1 &&
		memcmp(data, "OPTIONS /", sizeof("OPTIONS /") - 1) == 0)
		request.method = MH_SIGNAL_OPTIONS;
	else
		return MH_SIGNAL_NOT_FOUND;

	host = length = type = origin = real_ip = method = headers = 0;
	fields = 0;
	line = end + LINE_END_BYTES;

	while (line < header_size - LINE_END_BYTES) {
		if (++fields > SIGNAL_HEADER_FIELDS)
			return MH_SIGNAL_HEADER_TOO_LARGE;

		for (end = line; end < header_size && data[end] != '\r'; end++)
			;

		for (colon = line; colon < end && name_byte(data[colon]);
			colon++)
			;

		if (colon == line || colon == end || data[colon] != ':')
			return MH_SIGNAL_BAD_REQUEST;

		name.data = data + line;
		name.size = colon - line;
		i = colon + 1;

		while (i < end && (data[i] == ' ' || data[i] == '\t'))
			i++;

		value.data = data + i;
		value.size = end - i;

		while (value.size != 0 &&
			(value.data[value.size - 1] == ' ' ||
				value.data[value.size - 1] == '\t'))
			value.size--;

		if (equal(name, "host")) {
			if (host++ || value.size == 0)
				return MH_SIGNAL_BAD_REQUEST;
		} else if (equal(name, "content-length")) {
			if (length++)
				return MH_SIGNAL_BAD_REQUEST;

			status = content_length(value, &request.body_size);

			if (status != MH_SIGNAL_READY)
				return status;
		} else if (equal(name, "content-type")) {
			if (type++)
				return MH_SIGNAL_BAD_REQUEST;

			if (!equal(value, "application/sdp"))
				return MH_SIGNAL_UNSUPPORTED_MEDIA;
		} else if (equal(name, "transfer-encoding") ||
			equal(name, "content-encoding"))
			return MH_SIGNAL_BAD_REQUEST;
		else if (equal(name, "expect"))
			return MH_SIGNAL_EXPECTATION_FAILED;
		else if (equal(name, "origin")) {
			if (origin++ ||
				!copy_field(value, request.origin,
					sizeof(request.origin)) ||
				!mh_signal_origin_valid(request.origin))
				return MH_SIGNAL_BAD_REQUEST;
		} else if (equal(name, "x-real-ip")) {
			if (real_ip++ ||
				!copy_field(value, request.real_ip,
					sizeof(request.real_ip)))
				return MH_SIGNAL_BAD_REQUEST;
		} else if (equal(name, "access-control-request-method")) {
			if (method++ || !equal(value, "POST"))
				return MH_SIGNAL_BAD_REQUEST;
		} else if (equal(name, "access-control-request-headers")) {
			if (headers++ || !equal(value, "content-type"))
				return MH_SIGNAL_BAD_REQUEST;
		}

		line = end + LINE_END_BYTES;
	}

	if (!host ||
		(request.method == MH_SIGNAL_POST &&
			(!length || !type || request.body_size == 0)) ||
		(request.method == MH_SIGNAL_OPTIONS &&
			(!origin || !method || request.body_size != 0)))
		return MH_SIGNAL_BAD_REQUEST;

	if (size - header_size < request.body_size)
		return MH_SIGNAL_NEED_MORE;

	if (size - header_size != request.body_size)
		return MH_SIGNAL_BAD_REQUEST;

	*output = request;

	return MH_SIGNAL_READY;
}
