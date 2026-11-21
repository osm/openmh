#ifndef MH_WEBRTC_STUN_AUTH_H
#define MH_WEBRTC_STUN_AUTH_H

#include "backends/network/webrtc_stun.h"

/*
 * Native backend-private short-term credential operations (RFC 8489).
 * Check requires a binding request with exact username, MESSAGE-INTEGRITY and
 * FINGERPRINT. Username/password are terminated text (max 513/256 bytes).
 * The caller checks ICE role, priority and nomination after authentication.
 * No remote address/state may be changed before authentication succeeds.
 * Errors preserve output; success borrows the input packet.
 */
enum mh_error_code	mh_webrtc_stun_check(const unsigned char *data,
	size_t size, const char *username, const char *password,
	struct mh_stun_message *output, struct mh_error *error_out);

/*
 * Respond only to an authenticated request. Address contains 4/16 network-order
 * bytes from the received UDP endpoint, not untrusted message attributes.
 * Write XOR-MAPPED-ADDRESS, MESSAGE-INTEGRITY and FINGERPRINT. No overlap
 * between input/output. Errors preserve output and written size.
 */
enum mh_error_code	mh_webrtc_stun_reply(
	const struct mh_stun_message *request, const unsigned char *address,
	size_t address_size, unsigned int port, const char *password,
	unsigned char *output, size_t capacity, size_t *written,
	struct mh_error *error_out);

#endif
