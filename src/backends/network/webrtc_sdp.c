#include <stdio.h>
#include <string.h>

#include "backends/network/webrtc_sdp.h"
#include "engine/core/text.h"

enum {
	UFRAG = 1,
	PASSWORD = 2,
	FINGERPRINT = 4,
	SETUP = 8
};

struct credentials {
	char		ufrag[MH_RTC_ICE_BYTES], password[MH_RTC_ICE_BYTES];
	unsigned char	fingerprint[32];
	unsigned int	present;
};

static int
ice_text(const char *text, size_t capacity, size_t minimum)
{
	size_t i;
	unsigned char c;

	for (i = 0; i < capacity; i++) {
		c = (unsigned char)text[i];

		if (c == 0)
			return i >= minimum;

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '+' || c == '/'))
			return 0;
	}

	return 0;
}

static int
mid_text(const char *text, size_t capacity)
{
	size_t i;
	unsigned char c;

	for (i = 0; i < capacity; i++) {
		c = (unsigned char)text[i];

		if (c == 0)
			return i != 0;

		if (c < 33 || c > 126)
			return 0;
	}

	return 0;
}

static int
decimal(const char *text, unsigned long limit, unsigned long *output)
{
	unsigned long value;
	unsigned int digit;

	if (*text == 0 || (text[0] == '0' && text[1] != 0))
		return 0;

	value = 0;

	while (*text != 0) {
		if (*text < '0' || *text > '9')
			return 0;

		digit = (unsigned int)(*text++ - '0');

		if (value > (limit - digit) / 10)
			return 0;

		value = value * 10 + digit;
	}

	*output = value;

	return 1;
}

static int
hex_digit(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';

	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;

	return -1;
}

static int
fingerprint_read(const char *text, unsigned char output[32])
{
	unsigned int i;
	int a, b;

	if (strncmp(text, "sha-256 ", 8) != 0 || strlen(text + 8) != 95)
		return 0;

	text += 8;

	for (i = 0; i < 32; i++) {
		a = hex_digit((unsigned char)text[i * 3]);
		b = hex_digit((unsigned char)text[i * 3 + 1]);

		if (a < 0 || b < 0 || (i < 31 && text[i * 3 + 2] != ':'))
			return 0;

		output[i] = (unsigned char)(a * 16 + b);
	}

	return 1;
}

/* 1=recognized, 0=unrelated attribute, -1=invalid/duplicate. */
static int
credential_read(struct credentials *scope, const char *line)
{
	unsigned int field;
	int valid;

	field = 0;
	valid = 1;

	if (strncmp(line, "a=ice-ufrag:", 12) == 0) {
		field = UFRAG;
		valid = ice_text(line + 12, sizeof(scope->ufrag), 4);

		if (valid)
			strcpy(scope->ufrag, line + 12);
	} else if (strncmp(line, "a=ice-pwd:", 10) == 0) {
		field = PASSWORD;
		valid = ice_text(line + 10, sizeof(scope->password), 22);

		if (valid)
			strcpy(scope->password, line + 10);
	} else if (strncmp(line, "a=fingerprint:", 14) == 0) {
		field = FINGERPRINT;
		valid = fingerprint_read(line + 14, scope->fingerprint);
	} else if (strncmp(line, "a=setup:", 8) == 0) {
		field = SETUP;
		valid = strcmp(line + 8, "actpass") == 0 ||
			strcmp(line + 8, "active") == 0;
	}

	if (!valid || (scope->present & field) != 0)
		return -1;

	scope->present |= field;

	return field != 0;
}

static int
media_read(char *line)
{
	char *port, *end;
	unsigned long number;

	if (strncmp(line, "m=application ", 14) != 0)
		return 0;

	port = line + 14;
	end = strchr(port, ' ');

	if (end == NULL)
		return 0;

	*end++ = 0;

	return decimal(port, 65535, &number) && number != 0 &&
		strcmp(end, "UDP/DTLS/SCTP webrtc-datachannel") == 0;
}

enum mh_error_code
mh_rtc_offer_read(const unsigned char *data, size_t size,
	struct mh_rtc_offer *output, struct mh_error *error_out)
{
	struct mh_rtc_offer offer;
	struct credentials session, media, *scope;
	char line[1024];
	size_t offset, start, length, i;
	unsigned long number;
	unsigned int headers, seen;
	int media_seen, result;

	if (data == NULL || output == NULL || size == 0 ||
		size > MH_RTC_SDP_BYTES)
		goto invalid;

	memset(&offer, 0, sizeof(offer));
	memset(&session, 0, sizeof(session));
	memset(&media, 0, sizeof(media));
	offer.maximum_message = 65536UL;
	headers = seen = 0;
	media_seen = 0;

	for (offset = 0; offset < size;) {
		start = offset;

		while (offset < size && data[offset] != '\n')
			offset++;

		if (offset == size)
			goto invalid;

		length = offset++ - start;

		if (length != 0 && data[start + length - 1] == '\r')
			length--;

		if (length < 2 || length >= sizeof(line) ||
			data[start + 1] != '=')
			goto invalid;

		for (i = 0; i < length; i++)
			if (data[start + i] < 32 || data[start + i] > 126)
				goto invalid;

		memcpy(line, data + start, length);
		line[length] = 0;

		if (start == 0 && strcmp(line, "v=0") != 0)
			goto invalid;

		if (line[0] == 'v' || line[0] == 'o' || line[0] == 's' ||
			line[0] == 't') {
			i = line[0] == 'v'	 ? 1
				: line[0] == 'o' ? 2
				: line[0] == 's' ? 4
						: 8;

			if (media_seen || (headers & i) != 0 || length == 2)
				goto invalid;

			headers |= (unsigned int)i;
			continue;
		}

		if (line[0] == 'm') {
			if (media_seen || headers != 15 || !media_read(line))
				goto invalid;

			media_seen = 1;
			continue;
		}

		if (strcmp(line, "a=ice-lite") == 0 ||
			strcmp(line, "a=inactive") == 0)
			goto invalid;

		scope = media_seen ? &media : &session;
		result = credential_read(scope, line);

		if (result < 0)
			goto invalid;

		if (result != 0)
			continue;

		if (strncmp(line, "a=mid:", 6) == 0) {
			if (!media_seen || (seen & 1) != 0 ||
				!mid_text(line + 6, sizeof(offer.mid)))
				goto invalid;

			strcpy(offer.mid, line + 6);
			seen |= 1;
		} else if (strncmp(line, "a=sctp-port:", 12) == 0) {
			if (!media_seen || (seen & 2) != 0 ||
				!decimal(line + 12, 65535, &number) ||
				number == 0)
				goto invalid;

			offer.sctp_port = (unsigned int)number;
			seen |= 2;
		} else if (strncmp(line, "a=max-message-size:", 19) == 0) {
			if (!media_seen || (seen & 4) != 0 ||
				!decimal(line + 19, 0xffffffffUL, &number))
				goto invalid;

			offer.maximum_message = number;
			seen |= 4;
		}
	}

	if (!media_seen || (seen & 3) != 3 ||
		(session.present | media.present) != 15 ||
		(offer.maximum_message != 0 &&
			offer.maximum_message < MH_RTC_MESSAGE_BYTES))
		goto invalid;

	strcpy(offer.ufrag,
		(media.present & UFRAG) ? media.ufrag : session.ufrag);
	strcpy(offer.password,
		(media.present & PASSWORD) ? media.password : session.password);
	memcpy(offer.fingerprint,
		(media.present & FINGERPRINT) ? media.fingerprint
					: session.fingerprint,
		32);
	*output = offer;
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

invalid:

	return mh_error_set(error_out, MH_ERROR_INVALID,
		"invalid or unsupported browser data-channel offer");
}

enum mh_error_code
mh_rtc_answer_write(const struct mh_rtc_offer *offer,
	const struct mh_rtc_answer *answer, char *output, size_t capacity,
	size_t *written, struct mh_error *error_out)
{
	static const char hex[] = "0123456789ABCDEF";
	char text[2048], fingerprint[96], address[64], header[512], tail[512];
	unsigned int i;
	size_t size;

	if (offer == NULL || answer == NULL || output == NULL ||
		written == NULL || !mid_text(offer->mid, sizeof(offer->mid)) ||
		!ice_text(answer->ufrag, sizeof(answer->ufrag), 4) ||
		!ice_text(answer->password, sizeof(answer->password), 22) ||
		(answer->address_size != 4 && answer->address_size != 16) ||
		answer->udp_port == 0 || answer->udp_port > 65535 ||
		answer->sctp_port == 0 || answer->sctp_port > 65535 ||
		answer->session_id > 0xffffffffUL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ICE-lite answer configuration");

	for (i = 0; i < 32; i++) {
		fingerprint[i * 3] = hex[answer->fingerprint[i] >> 4];
		fingerprint[i * 3 + 1] = hex[answer->fingerprint[i] & 15];
		fingerprint[i * 3 + 2] = i == 31 ? 0 : ':';
	}

	if (answer->address_size == 4)
		sprintf(address, "%u.%u.%u.%u",
			(unsigned int)answer->address[0],
			(unsigned int)answer->address[1],
			(unsigned int)answer->address[2],
			(unsigned int)answer->address[3]);
	else
		sprintf(address, "%x:%x:%x:%x:%x:%x:%x:%x",
			answer->address[0] * 256U + answer->address[1],
			answer->address[2] * 256U + answer->address[3],
			answer->address[4] * 256U + answer->address[5],
			answer->address[6] * 256U + answer->address[7],
			answer->address[8] * 256U + answer->address[9],
			answer->address[10] * 256U + answer->address[11],
			answer->address[12] * 256U + answer->address[13],
			answer->address[14] * 256U + answer->address[15]);

	/* All fields above have fixed bounds; both formatted fragments fit. */
	sprintf(header,
		"v=0\r\no=- %lu 1 IN IP4 127.0.0.1\r\ns=OpenMH\r\nt=0 0\r\na=ice-lite\r\na=group:BUNDLE %s\r\nm=application %u UDP/DTLS/SCTP webrtc-datachannel\r\nc=IN IP%u %s\r\na=mid:%s\r\na=setup:passive\r\na=ice-ufrag:",
		answer->session_id, offer->mid, answer->udp_port,
		answer->address_size == 4 ? 4U : 6U, address, offer->mid);
	sprintf(tail,
		"\r\na=fingerprint:sha-256 %s\r\na=sctp-port:%u\r\na=max-message-size:%u\r\na=candidate:1 1 UDP 2130706431 %s %u typ host\r\na=end-of-candidates\r\n",
		fingerprint, answer->sctp_port, MH_RTC_MESSAGE_BYTES, address,
		answer->udp_port);
	text[0] = 0;

	if (mh_text_append(text, sizeof(text), header, NULL) != MH_ERROR_NONE ||
		mh_text_append(text, sizeof(text), answer->ufrag, NULL) !=
			MH_ERROR_NONE ||
		mh_text_append(text, sizeof(text), "\r\na=ice-pwd:", NULL) !=
			MH_ERROR_NONE ||
		mh_text_append(text, sizeof(text), answer->password, NULL) !=
			MH_ERROR_NONE ||
		mh_text_append(text, sizeof(text), tail, NULL) != MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"ICE-lite answer exceeds internal capacity");

	size = strlen(text);

	if (size >= capacity)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"ICE-lite answer output too small");

	memcpy(output, text, size + 1);
	*written = size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
