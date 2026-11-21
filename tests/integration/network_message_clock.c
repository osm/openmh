#include <stdio.h>
#include <string.h>

#include "../support/net_virtual_io.h"
#include "client/diagnostics/runtime_network.h"
#include "client/session/network_race.h"
#include "engine/core/error.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "message clock line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_race_context race_context;
static struct mh_net_race race;
static struct mh_network_race driving;

static int
observe(double published, double present, unsigned long result[5])
{
	struct mh_hud_state hud;
	struct mh_message_view view;
	unsigned int i;

	CHECK(published >= 0 && published < 4294967.296 && present >= 0 &&
		present < 4294967.296);
	mh_messages_clear(&driving.messages);
	race.field.cars[0].frame.seconds = published;
	CHECK(driving.race_hooks.message(&driving, "clock", NULL, 0x112233UL,
		NULL) == MH_ERROR_NONE);
	race.field.cars[0].frame.seconds = present;
	memset(&hud, 0, sizeof(hud));
	driving.race_hooks.hud(&driving, &hud);
	CHECK(hud.messages == &driving.messages &&
		hud.message_ms == hud.chat_ms);
	CHECK(mh_messages_view(hud.messages, hud.message_ms, 480, 12, &view,
		NULL) == MH_ERROR_NONE);
	result[0] = driving.messages.rows[0].milliseconds;
	result[1] = 0;

	for (i = 0; i < view.count; i++)
		result[1] |= 1U << view.lines[i].row;

	result[2] = (unsigned long)view.sound;
	result[3] = hud.chat_ms % 500 > 250;
	result[4] = hud.chat_ms;

	return 0;
}

static int
run(void)
{
	struct mh_net_session_config config;
	struct mh_net_session *session;
	struct mh_network *network;
	unsigned long result[5];
	unsigned int i;

	memset(&config, 0, sizeof(config));
	config.provider = 2;
	config.car_count = 10;
	config.laps = 3;
	strcpy(config.track, "RedRock");
	strcpy(config.profile.car, "Kobra");
	strcpy(config.profile.horn, "Honker1");
	config.profile.update_rate = 15;
	CHECK(mh_network_open(1, "127.0.0.1", "127.0.0.1", 1998, &config,
		&network, NULL) == MH_ERROR_NONE);
	session = mh_network_session(network);
	session->phase = MH_NET_RACING;
	session->replication.configured = 1;
	race.field.network = 1;
	race.field.count = 1;
	race.field.cars[0].frame.active = 1;
	race_context.scene.assets.view_width = 640;
	race_context.results.count = 1;

	for (i = 0; i < 256; i++)
		race_context.hud.message_advance[i] = 1;

	CHECK(mh_network_race_init(network, &race, &race_context, &driving,
		NULL) == MH_ERROR_NONE);

	{
		mh_test_net_advance(99000);
		CHECK(observe(8, 8.25, result) == 0 && result[0] == 8000 &&
			result[1] == 1 && result[2] == 1 && result[3] == 0 &&
			result[4] == 8250);

		/* Binary64 8.251 lies just below 8251 ms before truncation. */
		CHECK(observe(8, 8.251, result) == 0 && result[3] == 0 &&
			result[4] == 8250);
		CHECK(observe(8, 8.2515, result) == 0 && result[3] == 1);
		CHECK(observe(8, 8.5, result) == 0 && result[2] == 0);
		CHECK(observe(8, 13, result) == 0 && result[1] == 0);
		CHECK(observe(8, 0, result) == 0 && result[0] == 8000 &&
			result[1] == 31);
		puts("network messages: sampled stamps, cursor, expiry, sound and independent transport time pass");
	}

	mh_network_close(network);

	return 0;
}

int
main(void)
{
	return run();
}
