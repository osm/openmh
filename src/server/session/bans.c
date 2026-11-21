#include <math.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "server/session/bans.h"

#define CLOCK_LIMIT_MS 1e12

void
mh_server_bans_init(
	struct mh_server_bans *bans, const struct mh_server_config *config)
{
	unsigned int i;

	memset(bans, 0, sizeof(*bans));

	for (i = 0; i < config->ban_count; i++)
		strcpy(bans->entries[i].address, config->bans[i]);
}

enum mh_error_code
mh_server_bans_add(struct mh_server_bans *bans, const char *address,
	unsigned long duration_ms, double now_ms, struct mh_error *error_out)
{
	struct mh_server_ban *entry;
	unsigned int i;

	if (bans == NULL || address == NULL ||
		!mh_text_field_valid(address, MH_SERVER_BAN_ADDRESS_BYTES) ||
		address[0] == 0 || duration_ms > MH_U32_MAX ||
		!(now_ms >= 0 && now_ms <= CLOCK_LIMIT_MS))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid dedicated ban");

	/* First empty entry, without deduplication or expiry here. */
	for (i = 0; i < MH_SERVER_BANS; i++) {
		entry = &bans->entries[i];

		if (entry->address[0] != 0)
			continue;

		strcpy(entry->address, address);
		entry->expires_ms =
			duration_ms == 0 ? 0 : floor(now_ms + duration_ms);

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_error_set(
		error_out, MH_ERROR_RANGE, "dedicated ban table full");
}

void
mh_server_bans_expire(struct mh_server_bans *bans, double now_ms)
{
	struct mh_server_ban *entry;
	unsigned int i;

	for (i = 0; i < MH_SERVER_BANS; i++) {
		entry = &bans->entries[i];

		/* Equality remains banned; zero expiry never expires. */
		if (entry->address[0] != 0 && entry->expires_ms != 0 &&
			now_ms > entry->expires_ms)
			entry->address[0] = 0;
	}
}

int
mh_server_bans_contains(const struct mh_server_bans *bans, const char *address)
{
	unsigned int i;

	for (i = 0; i < MH_SERVER_BANS; i++)
		if (bans->entries[i].address[0] != 0 &&
			strcmp(bans->entries[i].address, address) == 0)
			return 1;

	return 0;
}

static int
accept_address(void *context, const struct mh_transport_address *address,
	double now_ms)
{
	struct mh_server_bans *bans;

	bans = context;
	mh_server_bans_expire(bans, now_ms);

	return !mh_server_bans_contains(bans, address->host);
}

void
mh_server_bans_filter(
	struct mh_server_bans *bans, struct mh_transport_filter *filter)
{
	filter->accept = accept_address;

	filter->context = bans;
	filter->rejection = "Connection denied: You're banned!";
}
