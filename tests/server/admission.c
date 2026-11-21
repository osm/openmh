#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "server/session/admission.h"

static int failures;

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

int
main(void)
{
	struct mh_server_config config;
	struct mh_server_admission admission, saved;
	struct mh_net_roster roster;
	struct mh_net_join join;
	struct mh_net_message reply;
	unsigned int i;

	mh_server_config_defaults(&config);
	mh_server_admission_reset(&admission);
	memset(&roster, 0, sizeof(roster));
	memset(&join, 0, sizeof(join));
	join.provider = MH_NET_PROVIDER_LANINET;
	join.version_bits = MH_NET_ORIGINAL_VERSION_BITS;

	for (i = 1; i <= 12; i++) {
		check(mh_server_admission_join(&admission, config.max_players,
			config.password, &roster, &join, 100, &reply,
			NULL) == MH_ERROR_NONE &&
				reply.type == MH_NET_ACCEPT &&
				reply.player == i,
			"twelve admissions skip even an empty server slot");
	}

	check(mh_server_admission_join(&admission, config.max_players,
		config.password, &roster, &join, 5100, &reply,
		NULL) == MH_ERROR_NONE &&
			reply.type == MH_NET_REJECT && reply.reason == 1 &&
			strcmp(reply.text, "Server full!") == 0,
		"reservations remain occupied at exactly five seconds");
	check(mh_server_admission_join(&admission, config.max_players,
		config.password, &roster, &join, 5100.5, &reply,
		NULL) == MH_ERROR_NONE &&
			reply.type == MH_NET_ACCEPT && reply.player == 1 &&
			admission.reserved_ms[1] == 5100 &&
			admission.reserved_ms[2] == 100 &&
			admission.reserved == 8190,
		"expire only reached slots, truncate stamp");

	config.max_players = 2;
	roster.players[1].flags = MH_NET_FLAG_LOBBY;
	roster.players[2].flags = MH_NET_FLAG_RACING;
	check(mh_server_admission_join(&admission, config.max_players,
		config.password, &roster, &join, 11000, &reply,
		NULL) == MH_ERROR_NONE &&
			reply.reason == 1 && admission.reserved == 8184,
		"occupied slots survive reservation expiry; configured limit applies");

	saved = admission;
	strcpy(config.password, "Secret");
	join.provider = MH_NET_PROVIDER_IP;
	join.version_bits = 0x3f800000UL;
	check(mh_server_admission_join(&admission, config.max_players,
		config.password, &roster, &join, 12000, &reply,
		NULL) == MH_ERROR_NONE &&
			reply.reason == 4,
		"provider mismatch precedes version and password");
	join.provider = MH_NET_PROVIDER_LANINET;
	check(mh_server_admission_join(&admission, config.max_players,
		config.password, &roster, &join, 12000, &reply,
		NULL) == MH_ERROR_NONE &&
			reply.reason == 3 &&
			strcmp(reply.text, "Server v2.17, Client v1.00") == 0,
		"version mismatch precedes password");
	join.version_bits = MH_NET_ORIGINAL_VERSION_BITS;
	check(mh_server_admission_join(&admission, config.max_players,
		config.password, &roster, &join, 12000, &reply,
		NULL) == MH_ERROR_NONE &&
			reply.reason == 2 &&
			memcmp(&saved, &admission, sizeof(saved)) == 0,
		"rejections do not expire reservations");
	strcpy(join.password, "sECRET");
	roster.players[2].flags = MH_NET_FLAG_RECORDING;
	check(mh_server_admission_join(&admission, config.max_players,
		config.password, &roster, &join, 12000, &reply,
		NULL) == MH_ERROR_NONE &&
			reply.type == MH_NET_ACCEPT && reply.player == 2,
		"password uses ASCII folding and presence excludes recording bit");

	saved = admission;
	memset(&reply, 0x5a, sizeof(reply));
	join.version_bits = 0x7f800000UL;
	check(mh_server_admission_join(&admission, config.max_players,
		config.password, &roster, &join, 12000, &reply,
		NULL) == MH_ERROR_INVALID &&
			((unsigned char *)&reply)[0] == 0x5a &&
			memcmp(&saved, &admission, sizeof(saved)) == 0,
		"nonfinite version is invalid without changing state or reply");
	join.version_bits = MH_NET_ORIGINAL_VERSION_BITS;
	config.max_players = 13;
	check(mh_server_admission_join(&admission, config.max_players,
		config.password, &roster, &join, 12000, &reply,
		NULL) == MH_ERROR_INVALID,
		"invalid configured capacity cannot index beyond twelve racers");

	mh_server_admission_reset(&admission);
	memset(&roster, 0, sizeof(roster));
	check(mh_server_admission_join(&admission, 1, config.password, &roster,
		&join, MH_U32_MODULUS - 1, &reply,
		NULL) == MH_ERROR_NONE &&
			reply.type == MH_NET_ACCEPT,
		"reserve a slot before the wire clock wraps");
	check(mh_server_admission_join(&admission, 1, config.password, &roster,
		&join, MH_U32_MODULUS, &reply, NULL) == MH_ERROR_NONE &&
			reply.type == MH_NET_REJECT,
		"reservation remains occupied across the wire clock wrap");

	return failures != 0;
}
