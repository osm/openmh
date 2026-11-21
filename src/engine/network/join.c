#include <string.h>

#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/network/join.h"
#include "engine/network/protocol_profile.h"

#define PROVIDER_MAX 255

enum rejection_reason {
	REJECT_PASSWORD = 2,
	REJECT_VERSION = 3,
	REJECT_PROVIDER = 4
};

static int
password_matches(
	const struct mh_net_join *request, const char *password, int legacy)
{
	unsigned char proof[MH_SHA256_BYTES];
	const char *end;

	if (request->challenge.active) {
		end = memchr(password, 0, MH_NET_PASSWORD_BYTES);

		if (end == NULL)
			return 0;

		mh_sha256_hmac(password, (size_t)(end - password),
			request->challenge.nonce,
			sizeof(request->challenge.nonce), proof);

		return request->has_proof &&
			mh_digest_equal(proof, request->proof, sizeof(proof));
	}

	if (request->has_proof)
		return 0;

	if (legacy)
		return mh_ascii_equal_n(
			password, request->password, MH_NET_PASSWORD_BYTES);

	return strncmp(password, request->password, MH_NET_PASSWORD_BYTES) == 0;
}

enum mh_error_code
mh_net_join_check_timing(unsigned int provider, const char *password,
	unsigned int simulation_rate, const struct mh_net_join *request,
	struct mh_net_message *reply, struct mh_error *error_out)
{
	struct mh_net_message message;
	const struct mh_net_protocol *protocol;

	protocol = mh_net_protocol_for_rate(simulation_rate);

	if (provider > PROVIDER_MAX || password == NULL || request == NULL ||
		reply == NULL || request->provider > PROVIDER_MAX ||
		!mh_net_protocol_accepts(protocol, simulation_rate) ||
		request->version_bits > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network join check");

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_REJECT;

	if (request->provider != provider) {
		message.reason = REJECT_PROVIDER;
		strcpy(message.text, "Provider types not equal!");
	} else if (request->version_bits != protocol->version_bits) {
		message.reason = REJECT_VERSION;

		protocol->version_rejection(request->version_bits, &message);
	} else if (password[0] != 0 &&
		!password_matches(request, password, simulation_rate == 0)) {
		message.reason = REJECT_PASSWORD;
		strcpy(message.text, "Invalid password!");
	} else {
		mh_net_protocol_accept(protocol, simulation_rate, &message);
	}

	*reply = message;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_join_check(unsigned int provider, const char *password,
	const struct mh_net_join *request, struct mh_net_message *reply,
	struct mh_error *error_out)
{
	return mh_net_join_check_timing(
		provider, password, 0, request, reply, error_out);
}
