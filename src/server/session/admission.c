#include <math.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "engine/network/join.h"
#include "server/session/admission.h"

#define CLOCK_LIMIT_MS 1e12

enum {
	RESERVATION_TIMEOUT_MS = 5000,
	PROVIDER_MAX = 255,
	REJECT_FULL = 1
};

void
mh_server_admission_reset(struct mh_server_admission *admission)
{
	memset(admission, 0, sizeof(*admission));
}

int
mh_server_admission_reserved(const struct mh_server_admission *admission,
	unsigned int slot, double now_ms)
{
	return (admission->reserved & (1U << slot)) != 0 &&
		now_ms - admission->reserved_ms[slot] <= RESERVATION_TIMEOUT_MS;
}

static unsigned int
reserve_slot(struct mh_server_admission *admission, unsigned long capacity,
	const struct mh_net_roster *roster, unsigned int previous,
	double now_ms)
{
	unsigned int slot, bit;

	if (previous &&
		mh_server_admission_reserved(admission, previous, now_ms) &&
		!(roster->players[previous].flags & MH_NET_FLAG_PRESENT))
		return previous;

	if (previous)
		admission->reserved &= ~(1U << previous);

	for (slot = 1; slot <= capacity; slot++) {
		bit = 1U << slot;

		if ((admission->reserved & bit) &&
			!mh_server_admission_reserved(admission, slot, now_ms))
			admission->reserved &= ~bit;

		if ((roster->players[slot].flags & MH_NET_FLAG_PRESENT) ||
			(admission->reserved & bit))
			continue;

		admission->reserved |= bit;
		admission->reserved_ms[slot] = floor(now_ms);

		return slot;
	}

	return 0;
}

enum mh_error_code
mh_server_admission_join_timing(struct mh_server_admission *admission,
	unsigned long capacity, const char *password,
	const struct mh_net_roster *roster, unsigned int simulation_rate,
	unsigned int previous, const struct mh_net_join *request, double now_ms,
	struct mh_net_message *reply, struct mh_error *error_out)
{
	unsigned char bytes[4];
	unsigned int slot;
	double version;
	enum mh_error_code status;

	if (admission == NULL || password == NULL || roster == NULL ||
		request == NULL || reply == NULL || !(now_ms >= 0) ||
		now_ms > CLOCK_LIMIT_MS || capacity > MH_SERVER_RACERS ||
		previous > capacity || request->provider > PROVIDER_MAX ||
		request->version_bits > MH_U32_MAX ||
		memchr(password, 0, MH_NET_PASSWORD_BYTES) == NULL ||
		memchr(request->password, 0, sizeof(request->password)) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated admission request");

	version = 0;
	mh_write_u32le(bytes, request->version_bits);

	if (!mh_read_f32le(bytes, &version))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"nonfinite dedicated client version");

	status = mh_net_join_check_timing(MH_NET_PROVIDER_LANINET, password,
		simulation_rate, request, reply, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (reply->type == MH_NET_ACCEPT) {
		slot = reserve_slot(
			admission, capacity, roster, previous, now_ms);

		if (slot == 0) {
			reply->type = MH_NET_REJECT;
			reply->reason = REJECT_FULL;
			strcpy(reply->text, "Server full!");
		} else {
			reply->type = MH_NET_ACCEPT;
			reply->player = slot;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_admission_join(struct mh_server_admission *admission,
	unsigned long capacity, const char *password,
	const struct mh_net_roster *roster, const struct mh_net_join *request,
	double now_ms, struct mh_net_message *reply, struct mh_error *error_out)
{
	return mh_server_admission_join_timing(admission, capacity, password,
		roster, 0, 0, request, now_ms, reply, error_out);
}
