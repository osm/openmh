#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/network/browser_provider.h"
#include "client/application/multiplayer.h"
#include "client/hosts/browser_multiplayer_api.h"

static int failures;
#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "line %d: %s\n", __LINE__, #test);     \
			failures++;                                            \
		}                                                              \
	} while (0)

/* Controlled application/backend boundary; no game assets or browser SDK. */
struct mh_application {
	int	available;
};

struct mh_transport_target {
	int	owned;
};
static struct mh_multiplayer_snapshot view;
static int refuse_join, closes;
static struct mh_transport_target *owned;
static char copied_url[MH_BROWSER_SIGNAL_URL_BYTES];
static char copied_user[MH_BROWSER_ICE_CREDENTIAL_BYTES];
static unsigned int copied_count;

int
mh_application_network_available(const struct mh_application *app)
{
	return app != NULL && app->available;
}

enum mh_error_code
mh_browser_network_target(const struct mh_browser_network_config *config,
	struct mh_transport_target **output, struct mh_error *error_out)
{
	if (config->signaling_url[0] == 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing test URL");

	strcpy(copied_url, config->signaling_url);
	copied_count = config->ice_server_count;
	copied_user[0] = 0;

	if (copied_count != 0)
		strcpy(copied_user, config->ice_servers[0].username);

	*output = calloc(1, sizeof(**output));
	CHECK(*output != NULL);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_transport_target_close(struct mh_transport_target *target)
{
	if (target != NULL) {
		closes++;
		free(target);
	}
}

enum mh_error_code
mh_application_join_network(struct mh_application *app,
	struct mh_transport_target *target,
	const struct mh_race_request *settings, const char *password,
	struct mh_error *error_out)
{
	CHECK(app != NULL && settings->car == 3);

	CHECK(strcmp(password, "secret") == 0);

	if (refuse_join)
		return mh_error_set(error_out, MH_ERROR_STATE, "test busy");

	owned = target;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_application_network_snapshot(const struct mh_application *app,
	struct mh_multiplayer_snapshot *output)
{
	memset(output, 0, sizeof(*output));

	if (app != NULL)
		*output = view;
}

const char *
mh_application_race_error(const struct mh_application *app)
{
	(void)app;

	return "test async failure";
}

enum mh_error_code
mh_application_leave_network(
	struct mh_application *app, struct mh_error *error_out)
{
	(void)app;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_application_network_return(
	struct mh_application *app, struct mh_error *error_out)
{
	return mh_application_leave_network(app, error_out);
}

enum mh_error_code
mh_application_network_player(struct mh_application *app, const char *name,
	const char *team, unsigned long color, struct mh_error *error_out)
{
	CHECK(app != NULL);

	CHECK(strcmp(name, "Driver Name") == 0);
	CHECK(team[0] == 0 && color == 0x123456UL);

	return view.state == MH_NETWORK_LOBBY
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_STATE, "not in lobby");
}

enum mh_error_code
mh_application_network_show_fps(
	struct mh_application *app, int enabled, struct mh_error *error_out)
{
	CHECK(app != NULL);

	if (view.state != MH_NETWORK_LOBBY)
		return mh_error_set(error_out, MH_ERROR_STATE, "not in lobby");

	if (enabled != 0 && enabled != 1)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid FPS setting");

	view.show_fps = enabled;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_application_network_chat(struct mh_application *app, const char *text,
	struct mh_lobby_command_output *output, struct mh_error *error_out)
{
	if (strcmp(text, "/name Driver") == 0) {
		output->count = 1;
		strcpy(output->events[0].text, "Name changed to: Driver");
	} else if (strcmp(text, "/fail") == 0)
		return mh_error_set(error_out, MH_ERROR_STATE, "not in lobby");

	else
		CHECK(strcmp(text, "hello") == 0);

	return mh_application_leave_network(app, error_out);
}

enum mh_error_code
mh_application_network_ready(
	struct mh_application *app, int ready, struct mh_error *error_out)
{
	CHECK(ready == 1);

	return mh_application_leave_network(app, error_out);
}

enum mh_error_code
mh_application_network_toggle_ready(
	struct mh_application *app, struct mh_error *error_out)
{
	return mh_application_leave_network(app, error_out);
}

int
main(void)
{
	struct mh_application app;
	struct mh_race_request settings;
#ifdef MH_BROWSER_NETWORK
	unsigned int i;
	char long_url[MH_BROWSER_ICE_URL_BYTES + 1];
#endif

	app.available = 1;
	memset(&settings, 0, sizeof(settings));
	settings.car = 3;
	CHECK(!mh_browser_multiplayer_available());
	mh_browser_multiplayer_attach(&app, &settings);
#ifdef MH_BROWSER_NETWORK
	CHECK(mh_browser_multiplayer_available());
	CHECK(mh_browser_multiplayer_configure("https://test.invalid/", 0));
	memset(long_url, 'a', sizeof(long_url));
	long_url[sizeof(long_url) - 1] = 0;
	CHECK(!mh_browser_multiplayer_ice(long_url, "", ""));

	for (i = 0; i < MH_BROWSER_ICE_SERVERS; i++)
		CHECK(mh_browser_multiplayer_ice(
			"turn:test.invalid", "user", "credential"));

	CHECK(!mh_browser_multiplayer_ice("turn:excess.invalid", "", ""));
	CHECK(mh_browser_multiplayer_join("secret"));
	CHECK(owned != NULL && closes == 0 &&
		copied_count == MH_BROWSER_ICE_SERVERS);
	CHECK(strcmp(copied_user, "user") == 0);
	CHECK(strcmp(copied_url, "https://test.invalid/") == 0);
	mh_transport_target_close(owned);
	owned = NULL;

	/*
	 * Credentials/configuration are consumed; another join needs
	 * configuration.
	 */
	CHECK(!mh_browser_multiplayer_join("secret"));
	CHECK(mh_browser_multiplayer_configure("https://second.invalid/", 1));
	refuse_join = 1;
	CHECK(!mh_browser_multiplayer_join("secret"));
	CHECK(closes == 2 && copied_count == 0);
	CHECK(strcmp(mh_browser_multiplayer_error(), "test busy") == 0);
#else
	CHECK(!mh_browser_multiplayer_available());
	CHECK(!mh_browser_multiplayer_configure("https://test.invalid/", 0));
	CHECK(!mh_browser_multiplayer_ice("stun:test.invalid", "", ""));
	CHECK(!mh_browser_multiplayer_join("secret"));
	CHECK(owned == NULL && closes == 0);
#endif
	CHECK(!mh_browser_multiplayer_show_fps(1));
	CHECK(!mh_browser_multiplayer_player("Driver Name", "", 0x123456UL));
	CHECK(strcmp(mh_browser_multiplayer_error(), "not in lobby") == 0);
	view.state = MH_NETWORK_LOBBY;
	CHECK(mh_browser_multiplayer_player("Driver Name", "", 0x123456UL));
	CHECK(!mh_browser_multiplayer_show_fps(2));
	CHECK(mh_browser_multiplayer_show_fps(1));
	CHECK(mh_browser_multiplayer_refresh() == MH_NETWORK_LOBBY);
	CHECK(mh_browser_multiplayer_number("showFPS", 0) == 1);
	CHECK(mh_browser_multiplayer_show_fps(0));
	CHECK(mh_browser_multiplayer_refresh() == MH_NETWORK_LOBBY);
	CHECK(mh_browser_multiplayer_number("showFPS", 0) == 0);
	view.car_count = 2;
	view.car_ids[0] = 3;
	view.car_ids[1] = 1;
	view.player = 1;
	view.roster.player_count = 2;
	view.roster.players[1].flags = MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY;
	strcpy(view.roster.players[1].name, "Dr\351ver");
	view.chat_count = 1;
	strcpy(view.chat[0].text, "hello");
	view.statistics_revision = 1;
	memset(view.last_race.rows[0].name, 'x', MH_NET_NAME_BYTES);
	CHECK(mh_browser_multiplayer_refresh() == MH_NETWORK_LOBBY);
	strcpy(view.chat[0].text, "changed after refresh");
	CHECK(strcmp(mh_browser_multiplayer_text("chat", 0), "hello") == 0);
	CHECK(strcmp(mh_browser_multiplayer_text("name", 1), "Dr\303\251ver") ==
		0);
	CHECK(strcmp(mh_browser_multiplayer_text("statName", 0), "") == 0);
	CHECK(strcmp(mh_browser_multiplayer_text("name", MH_NET_WIRE_PLAYERS),
		"") == 0);
	CHECK(strcmp(mh_browser_multiplayer_text("error", 0),
		"test async failure") == 0);
	CHECK(mh_browser_multiplayer_number("carCount", 0) == 2);
	CHECK(mh_browser_multiplayer_number("carId", 0) == 3);
	CHECK(mh_browser_multiplayer_number("carId", 1) == 1);
	CHECK(mh_browser_multiplayer_number("carId", 2) == -1);
	CHECK(mh_browser_multiplayer_number("ready", 1) == 1);
	CHECK(mh_browser_multiplayer_number("ready", MH_NET_WIRE_PLAYERS) ==
		-1);
	CHECK(mh_browser_multiplayer_number("unknown", 0) == -1);
	CHECK(mh_browser_multiplayer_chat("/name Driver"));
	CHECK(mh_browser_multiplayer_number("commandCount", 0) == 1);
	CHECK(strcmp(mh_browser_multiplayer_text("command", 0),
		"Name changed to: Driver") == 0);
	CHECK(strcmp(mh_browser_multiplayer_text("command", 1), "") == 0);
	CHECK(!mh_browser_multiplayer_chat("/fail"));
	CHECK(strcmp(mh_browser_multiplayer_error(), "not in lobby") == 0);
	CHECK(mh_browser_multiplayer_number("commandCount", 0) == 0);
	CHECK(mh_browser_multiplayer_chat("hello"));
	CHECK(mh_browser_multiplayer_number("commandCount", 0) == 0);
	CHECK(mh_browser_multiplayer_ready(1));
	CHECK(mh_browser_multiplayer_toggle_ready());
	CHECK(mh_browser_multiplayer_return());
	CHECK(mh_browser_multiplayer_leave());
	mh_browser_multiplayer_detach();
	CHECK(!mh_browser_multiplayer_available());
	CHECK(mh_browser_multiplayer_refresh() == MH_NETWORK_OFFLINE);
	CHECK(mh_browser_multiplayer_number("players", 0) == 0);

	return failures != 0;
}
