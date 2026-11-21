#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "server/session/bans.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "ban line %d: %s\n", __LINE__, #e);    \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_long_uptime(void)
{
	static const double starts[] = {(double)MH_U32_MODULUS - 30000,
		(double)MH_U32_MODULUS - 60000, (double)MH_U32_MODULUS + 1000,
		2 * (double)MH_U32_MODULUS + 1000};

	struct mh_server_bans bans;
	unsigned int i;
	double now;

	for (i = 0; i < sizeof(starts) / sizeof(starts[0]); i++) {
		memset(&bans, 0, sizeof(bans));
		now = starts[i];
		CHECK(mh_server_bans_add(&bans, "192.0.2.1", 60000, now,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_server_bans_add(&bans, "192.0.2.2", 0, now, NULL) ==
			MH_ERROR_NONE);

		mh_server_bans_expire(&bans, now);
		CHECK(mh_server_bans_contains(&bans, "192.0.2.1"));
		mh_server_bans_expire(&bans, now + 60000);
		CHECK(mh_server_bans_contains(&bans, "192.0.2.1"));
		mh_server_bans_expire(&bans, now + 60000.25);
		CHECK(!mh_server_bans_contains(&bans, "192.0.2.1"));
		CHECK(mh_server_bans_contains(&bans, "192.0.2.2"));
	}

	return 0;
}

int
main(void)
{
	struct mh_server_config config;
	struct mh_server_bans bans;
	struct mh_transport_filter filter;
	struct mh_transport_address address;
	unsigned int i;
	size_t line;
	static const char ipv6_config[] = "Ban 2001:db8:1234:5678::abcd\n";

	mh_server_config_defaults(&config);
	config.ban_count = 2;
	strcpy(config.bans[0], "192.0.2.1");
	strcpy(config.bans[1], "192.0.2.2");
	mh_server_bans_init(&bans, &config);
	mh_server_bans_filter(&bans, &filter);
	memset(&address, 0, sizeof(address));
	strcpy(address.host, "192.0.2.1");
	address.port = 16000;
	CHECK(!filter.accept(filter.context, &address, 1000));
	CHECK(strcmp(filter.rejection, "Connection denied: You're banned!") ==
		0);
	CHECK(mh_server_bans_add(&bans, "127.0.0.1", 60000, 1000, NULL) ==
		MH_ERROR_NONE);
	strcpy(address.host, "127.0.0.1");
	CHECK(!filter.accept(filter.context, &address, 61000));
	address.port++;
	CHECK(!filter.accept(filter.context, &address, 61000));
	CHECK(filter.accept(filter.context, &address, 61000.25));
	CHECK(mh_server_bans_contains(&bans, "192.0.2.1") &&
		mh_server_bans_contains(&bans, "192.0.2.2"));
	CHECK(mh_server_bans_add(&bans, "127.0.0.1", 0, 62000, NULL) ==
		MH_ERROR_NONE);
	CHECK(!filter.accept(filter.context, &address, 1000000));

	for (i = 3; i < MH_SERVER_BANS; i++)
		CHECK(mh_server_bans_add(&bans, "127.0.0.1", 0, 1000000,
			NULL) == MH_ERROR_NONE);

	CHECK(mh_server_bans_add(&bans, "192.0.2.3", 0, 1000000, NULL) ==
		MH_ERROR_RANGE);
	CHECK(!mh_server_bans_contains(&bans, "192.0.2.3"));
	CHECK(mh_server_config_parse(&config, ipv6_config,
		sizeof(ipv6_config) - 1, &line, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(config.bans[0], "2001:db8:1234:5678::abcd") == 0);
	mh_server_bans_init(&bans, &config);
	strcpy(address.host, "2001:db8:1234:5678::abcd");
	CHECK(!filter.accept(filter.context, &address, 0));
	CHECK(mh_server_bans_add(&bans, "2001:db8:abcd:ef01::1234", 60000, 0,
		NULL) == MH_ERROR_NONE);
	strcpy(address.host, "2001:db8:abcd:ef01::1234");
	CHECK(!filter.accept(filter.context, &address, 60000));
	CHECK(filter.accept(filter.context, &address, 60001));
	CHECK(check_long_uptime() == 0);

	return 0;
}
