#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <string.h>

#include "backends/network/webrtc_stun_auth.h"
#include "backends/network/webrtc_wire.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"

static enum mh_error_code
integrity(const unsigned char *data, size_t size, const char *password,
	unsigned char *digest, struct mh_error *error_out)
{
	unsigned int bytes;

	if (!mh_text_field_valid(password, MH_STUN_PASSWORD_MAX + 1) ||
		password[0] == 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid STUN password");

	if (HMAC(EVP_sha1(), password, (int)strlen(password), data, size,
		digest, &bytes) == NULL ||
		bytes != MH_STUN_INTEGRITY_BYTES)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot authenticate STUN message");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webrtc_stun_check(const unsigned char *data, size_t size,
	const char *username, const char *password,
	struct mh_stun_message *output, struct mh_error *error_out)
{
	struct mh_stun_message message;
	unsigned char signed_bytes[MH_STUN_PACKET_BYTES],
		digest[EVP_MAX_MD_SIZE];
	enum mh_error_code status;

	if (output == NULL ||
		!mh_text_field_valid(username, MH_STUN_USERNAME_MAX + 1) ||
		username[0] == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid STUN authentication arguments");

	status = mh_stun_read(data, size, &message, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (message.type != MH_STUN_BINDING_REQUEST ||
		message.unknown_required || message.integrity_offset == 0 ||
		message.fingerprint_offset == 0 || message.username == NULL ||
		message.username_size != strlen(username) ||
		memcmp(message.username, username, message.username_size) != 0)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"STUN credentials are missing or do not match");

	memcpy(signed_bytes, data, message.integrity_offset);

	/*
	 * HMAC excludes its own attribute; header length includes that
	 * attribute.
	 */
	mh_rtc_write16(signed_bytes + MH_STUN_LENGTH_OFFSET,
		(unsigned int)(message.integrity_offset +
			MH_STUN_ATTRIBUTE_BYTES + MH_STUN_INTEGRITY_BYTES -
			MH_STUN_HEADER_BYTES));
	status = integrity(signed_bytes, message.integrity_offset, password,
		digest, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (CRYPTO_memcmp(digest,
		data + message.integrity_offset + MH_STUN_ATTRIBUTE_BYTES,
		MH_STUN_INTEGRITY_BYTES) != 0)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"STUN integrity check failed");

	*output = message;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webrtc_stun_reply(const struct mh_stun_message *request,
	const unsigned char *address, size_t address_size, unsigned int port,
	const char *password, unsigned char *output, size_t capacity,
	size_t *written, struct mh_error *error_out)
{
	unsigned char packet[MH_STUN_HEADER_BYTES + MH_STUN_ATTRIBUTE_BYTES +
		MH_STUN_IPV6_MAPPED_BYTES + MH_STUN_ATTRIBUTE_BYTES +
		MH_STUN_INTEGRITY_BYTES + MH_STUN_ATTRIBUTE_BYTES +
		MH_STUN_FINGERPRINT_BYTES],
		digest[EVP_MAX_MD_SIZE];
	unsigned char *mapped;
	size_t integrity_offset, fingerprint_offset, length, i;
	unsigned long fingerprint;
	enum mh_error_code status;

	if (request == NULL || request->transaction == NULL ||
		request->type != MH_STUN_BINDING_REQUEST || address == NULL ||
		(address_size != MH_STUN_IPV4_BYTES &&
			address_size != MH_STUN_IPV6_BYTES) ||
		port == 0 || port > MH_U16_MAX || output == NULL ||
		written == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid STUN reply arguments");

	integrity_offset = MH_STUN_HEADER_BYTES + MH_STUN_ATTRIBUTE_BYTES +
		MH_STUN_MAPPED_HEADER_BYTES + address_size;
	fingerprint_offset = integrity_offset + MH_STUN_ATTRIBUTE_BYTES +
		MH_STUN_INTEGRITY_BYTES;
	length = fingerprint_offset + MH_STUN_ATTRIBUTE_BYTES +
		MH_STUN_FINGERPRINT_BYTES;

	if (capacity < length)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"STUN reply exceeds output buffer");

	memset(packet, 0, sizeof(packet));
	mh_rtc_write16(packet, MH_STUN_BINDING_SUCCESS);
	mh_rtc_write16(packet + MH_STUN_LENGTH_OFFSET,
		(unsigned int)(fingerprint_offset - MH_STUN_HEADER_BYTES));
	mh_rtc_write32(packet + MH_STUN_COOKIE_OFFSET, MH_STUN_COOKIE);
	memcpy(packet + MH_STUN_TRANSACTION_OFFSET, request->transaction,
		MH_STUN_TRANSACTION_BYTES);
	mh_rtc_write16(packet + MH_STUN_HEADER_BYTES, MH_STUN_ATTR_XOR_MAPPED);
	mh_rtc_write16(packet + MH_STUN_HEADER_BYTES + MH_STUN_ATTRIBUTE_LENGTH,
		(unsigned int)(address_size + MH_STUN_MAPPED_HEADER_BYTES));
	mapped = packet + MH_STUN_HEADER_BYTES + MH_STUN_ATTRIBUTE_BYTES;
	mapped[MH_STUN_ADDRESS_FAMILY] = address_size == MH_STUN_IPV4_BYTES
		? MH_STUN_IPV4
		: MH_STUN_IPV6;
	mh_rtc_write16(mapped + MH_STUN_ADDRESS_PORT,
		port ^ (unsigned int)(MH_STUN_COOKIE >> 16));

	for (i = 0; i < address_size; i++)
		mapped[MH_STUN_MAPPED_HEADER_BYTES + i] =
			address[i] ^ packet[MH_STUN_COOKIE_OFFSET + i];

	status = integrity(
		packet, integrity_offset, password, digest, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_rtc_write16(packet + integrity_offset, MH_STUN_ATTR_INTEGRITY);
	mh_rtc_write16(packet + integrity_offset + MH_STUN_ATTRIBUTE_LENGTH,
		MH_STUN_INTEGRITY_BYTES);
	memcpy(packet + integrity_offset + MH_STUN_ATTRIBUTE_BYTES, digest,
		MH_STUN_INTEGRITY_BYTES);
	mh_rtc_write16(packet + MH_STUN_LENGTH_OFFSET,
		(unsigned int)(length - MH_STUN_HEADER_BYTES));
	mh_rtc_write16(packet + fingerprint_offset, MH_STUN_ATTR_FINGERPRINT);
	mh_rtc_write16(packet + fingerprint_offset + MH_STUN_ATTRIBUTE_LENGTH,
		MH_STUN_FINGERPRINT_BYTES);
	fingerprint = mh_rtc_crc(packet, fingerprint_offset, (size_t)-1,
			MH_STUN_CRC_POLYNOMIAL) ^
		MH_STUN_FINGERPRINT_XOR;
	mh_rtc_write32(packet + fingerprint_offset + MH_STUN_ATTRIBUTE_BYTES,
		fingerprint);
	memcpy(output, packet, length);
	*written = length;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
