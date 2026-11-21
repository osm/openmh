#include <string.h>

#include "client/application/multiplayer.h"
#include "client/hosts/browser_multiplayer_api.h"
#include "engine/core/text.h"
#ifdef MH_BROWSER_NETWORK
#include "backends/network/browser_provider.h"
#endif

#define API_ERROR_BYTES 512
/* A legacy byte may occupy two UTF-8 bytes, including a final NUL. */
#define API_TEXT_BYTES (MH_LOBBY_COMMAND_TEXT * 2)

static struct mh_application *application;
static struct mh_multiplayer_snapshot snapshot;
static struct mh_lobby_command_output command_output;
static char failure[API_ERROR_BYTES], text_buffer[API_TEXT_BYTES];

#ifdef MH_BROWSER_NETWORK
static const struct mh_race_request *settings;

struct ice_entry {
	char	url[MH_BROWSER_ICE_URL_BYTES];
	char	username[MH_BROWSER_ICE_CREDENTIAL_BYTES];
	char	credential[MH_BROWSER_ICE_CREDENTIAL_BYTES];
};

static char signaling[MH_BROWSER_SIGNAL_URL_BYTES];
static struct ice_entry ice[MH_BROWSER_ICE_SERVERS];
static unsigned int ice_count;
static int relay;
#endif

static int
result(enum mh_error_code status, const struct mh_error *error)
{
	struct mh_error retained;

	failure[0] = 0;

	if (status == MH_ERROR_NONE)
		return 1;

	mh_error_copy(&retained, error, failure, sizeof(failure));

	return 0;
}

#ifndef MH_BROWSER_NETWORK
static int
unavailable(void)
{
	struct mh_error error;

	mh_error_set(&error, MH_ERROR_UNSUPPORTED,
		"This build does not include browser multiplayer.");

	return result(error.code, &error);
}
#endif

void
mh_browser_multiplayer_attach(
	struct mh_application *app, const struct mh_race_request *request)
{
	mh_browser_multiplayer_detach();

	application = app;
#ifdef MH_BROWSER_NETWORK
	settings = request;
#else
	(void)request;
#endif
}

void
mh_browser_multiplayer_detach(void)
{
	application = NULL;
	memset(&snapshot, 0, sizeof(snapshot));
	memset(&command_output, 0, sizeof(command_output));
	failure[0] = text_buffer[0] = 0;
#ifdef MH_BROWSER_NETWORK
	settings = NULL;
	memset(signaling, 0, sizeof(signaling));
	memset(ice, 0, sizeof(ice));
	ice_count = 0;
	relay = 0;
#endif
}

int
mh_browser_multiplayer_available(void)
{
#ifdef MH_BROWSER_NETWORK
	return mh_application_network_available(application);
#else

	return 0;
#endif
}

int
mh_browser_multiplayer_configure(const char *url, int relay_only)
{
#ifdef MH_BROWSER_NETWORK
	struct mh_error error;
	enum mh_error_code status;

	/* Clear previous credentials even if this attempt is invalid. */
	signaling[0] = 0;
	memset(ice, 0, sizeof(ice));
	ice_count = 0;

	if (url == NULL || url[0] == 0 ||
		(relay_only != 0 && relay_only != 1)) {
		mh_error_set(&error, MH_ERROR_INVALID,
			"Invalid signaling URL or relay policy.");
		return result(error.code, &error);
	}

	status = mh_text_copy(signaling, sizeof(signaling), url, &error);
	relay = relay_only;
	return result(status, &error);
#else
	(void)url;
	(void)relay_only;

	return unavailable();
#endif
}

int
mh_browser_multiplayer_ice(
	const char *url, const char *username, const char *credential)
{
#ifdef MH_BROWSER_NETWORK
	struct ice_entry entry;
	struct mh_error error;
	enum mh_error_code status;

	if (signaling[0] == 0 || ice_count == MH_BROWSER_ICE_SERVERS ||
		url == NULL || url[0] == 0 || username == NULL ||
		credential == NULL) {
		mh_error_set(&error, MH_ERROR_INVALID,
			"Invalid or excessive ICE server configuration.");
		return result(error.code, &error);
	}

	memset(&entry, 0, sizeof(entry));
	status = mh_text_copy(entry.url, sizeof(entry.url), url, &error);

	if (status == MH_ERROR_NONE)
		status = mh_text_copy(entry.username, sizeof(entry.username),
			username, &error);

	if (status == MH_ERROR_NONE)
		status = mh_text_copy(entry.credential,
			sizeof(entry.credential), credential, &error);

	if (status == MH_ERROR_NONE)
		ice[ice_count++] = entry;

	return result(status, &error);
#else
	(void)url;
	(void)username;
	(void)credential;

	return unavailable();
#endif
}

int
mh_browser_multiplayer_join(const char *password)
{
#ifdef MH_BROWSER_NETWORK
	struct mh_browser_network_config config;
	struct mh_browser_ice_server servers[MH_BROWSER_ICE_SERVERS];
	struct mh_transport_target *target;
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i;

	if (!mh_browser_multiplayer_available() || settings == NULL) {
		mh_error_set(&error, MH_ERROR_UNSUPPORTED,
			"Browser multiplayer is unavailable.");
		return result(error.code, &error);
	}

	memset(&config, 0, sizeof(config));
	config.signaling_url = signaling;
	config.relay_only = relay;
	config.ice_servers = servers;
	config.ice_server_count = ice_count;

	for (i = 0; i < ice_count; i++) {
		servers[i].url = ice[i].url;
		servers[i].username = ice[i].username;
		servers[i].credential = ice[i].credential;
	}

	target = NULL;
	status = mh_browser_network_target(&config, &target, &error);

	if (status == MH_ERROR_NONE) {
		status = mh_application_join_network(
			application, target, settings, password, &error);

		if (status != MH_ERROR_NONE)
			mh_transport_target_close(target);
	}

	/* The target owns copies; no signaling credentials need remain here. */
	memset(signaling, 0, sizeof(signaling));
	memset(ice, 0, sizeof(ice));
	ice_count = 0;
	return result(status, &error);
#else
	(void)password;

	return unavailable();
#endif
}

int
mh_browser_multiplayer_leave(void)
{
	struct mh_error error;
	enum mh_error_code status;

	status = mh_application_leave_network(application, &error);

	return result(status, &error);
}

int
mh_browser_multiplayer_return(void)
{
	struct mh_error error;
	enum mh_error_code status;

	status = mh_application_network_return(application, &error);

	return result(status, &error);
}

int
mh_browser_multiplayer_player(
	const char *name, const char *team, unsigned long color)
{
	struct mh_error error;
	enum mh_error_code status;

	status = mh_application_network_player(
		application, name, team, color, &error);

	return result(status, &error);
}

int
mh_browser_multiplayer_show_fps(int enabled)
{
	struct mh_error error;
	enum mh_error_code status;

	status = mh_application_network_show_fps(application, enabled, &error);

	return result(status, &error);
}

int
mh_browser_multiplayer_toggle_ready(void)
{
	struct mh_error error;
	enum mh_error_code status;

	status = mh_application_network_toggle_ready(application, &error);

	return result(status, &error);
}

int
mh_browser_multiplayer_chat(const char *text)
{
	struct mh_error error;
	enum mh_error_code status;

	memset(&command_output, 0, sizeof(command_output));
	status = mh_application_network_chat(
		application, text, &command_output, &error);

	return result(status, &error);
}

int
mh_browser_multiplayer_ready(int ready)
{
	struct mh_error error;
	enum mh_error_code status;

	status = mh_application_network_ready(application, ready, &error);

	return result(status, &error);
}

const char *
mh_browser_multiplayer_error(void)
{
	return failure[0] != 0 ? failure
			: mh_application_race_error(application);
}

int
mh_browser_multiplayer_refresh(void)
{
	mh_application_network_snapshot(application, &snapshot);

	return (int)snapshot.state;
}

double
mh_browser_multiplayer_number(const char *field, unsigned int index)
{
	const struct mh_net_profile *profile;
	const struct mh_net_statistics_row *row;

	if (field == NULL)
		return -1;

#define VALUE(key, expression)                                                 \
	if (strcmp(field, key) == 0)                                           \
	return (double)(expression)
	VALUE("commandCount", command_output.count);
	VALUE("player", snapshot.player);
	VALUE("players", snapshot.roster.player_count);
	VALUE("carCount", snapshot.car_count);

	if (index < snapshot.car_count && index < MH_CAR_CATALOG_LIMIT) {
		VALUE("carId", snapshot.car_ids[index]);
	}

	VALUE("mode", snapshot.roster.mode);
	VALUE("laps", snapshot.roster.laps);
	VALUE("showFPS", snapshot.show_fps);
	VALUE("finished", snapshot.finished);
	VALUE("chatCount", snapshot.chat_count);
	VALUE("chatRevision", snapshot.chat_revision);
	VALUE("noticeRevision", snapshot.notice_revision);
	VALUE("statisticsRevision", snapshot.statistics_revision);
	VALUE("resultsRevision", snapshot.results_revision);
	VALUE("statisticsCount",
		snapshot.statistics_revision != 0 ? MH_NET_STATISTICS_ROWS : 0);

	if (index < snapshot.roster.player_count &&
		index < MH_NET_WIRE_PLAYERS) {
		profile = &snapshot.roster.players[index];
		VALUE("present", (profile->flags & MH_NET_FLAG_PRESENT) != 0);
		VALUE("racing", (profile->flags & MH_NET_FLAG_RACING) != 0);
		VALUE("ready", (profile->flags & MH_NET_FLAG_READY) != 0);
		VALUE("passing", (profile->flags & MH_NET_FLAG_PASS) != 0);
		VALUE("color", profile->values[MH_NET_PROFILE_COLOR]);
		VALUE("points", profile->values[MH_NET_PROFILE_POINTS]);
		VALUE("races", profile->values[MH_NET_PROFILE_RACES]);
	}

	if (index < snapshot.chat_count && index < MH_NET_CHAT_LINES) {
		VALUE("chatPlayer", snapshot.chat[index].player);
		VALUE("chatColor", snapshot.chat[index].color);
	}

	if (index < MH_NET_STATISTICS_ROWS &&
		snapshot.statistics_revision != 0) {
		row = &snapshot.last_race.rows[index];
		VALUE("statPosition", row->position);
		VALUE("statColor", row->color);
		VALUE("statSpeed", row->top_speed_kmh);
		VALUE("statBestLap", row->best_lap_ms);
		VALUE("statTime", row->race_ms);
	}
#undef VALUE

	return -1;
}

/* Native names/chat use legacy bytes; the JavaScript string ABI is UTF-8. */
static const char *
legacy_text(const char *text, size_t capacity)
{
	size_t i, written;
	unsigned char byte;

	if (memchr(text, 0, capacity) == NULL)
		return "";

	written = 0;

	for (i = 0; i < capacity && text[i] != 0; i++) {
		byte = (unsigned char)text[i];

		if (written + 2 >= sizeof(text_buffer))
			return "";

		if (byte >= 128) {
			text_buffer[written++] = (char)(0xc0 | (byte >> 6));
			text_buffer[written++] = (char)(0x80 | (byte & 0x3f));
		} else
			text_buffer[written++] = (char)byte;
	}

	text_buffer[written] = 0;

	return text_buffer;
}

const char *
mh_browser_multiplayer_text(const char *field, unsigned int index)
{
	const struct mh_net_profile *profile;

	if (field == NULL)
		return "";

	if (strcmp(field, "error") == 0)
		return mh_application_race_error(application);

#define TEXT(key, value)                                                       \
	if (strcmp(field, key) == 0)                                           \
	return legacy_text(value, sizeof(value))
	if (index < command_output.count) {
		TEXT("command", command_output.events[index].text);
	}

	TEXT("track", snapshot.roster.track);
	TEXT("notice", snapshot.notice.text);

	if (index < snapshot.roster.player_count &&
		index < MH_NET_WIRE_PLAYERS) {
		profile = &snapshot.roster.players[index];
		TEXT("name", profile->name);
		TEXT("team", profile->team);
		TEXT("car", profile->car);
	}

	if (index < snapshot.chat_count && index < MH_NET_CHAT_LINES) {
		TEXT("chat", snapshot.chat[index].text);
	}

	if (index < MH_NET_STATISTICS_ROWS &&
		snapshot.statistics_revision != 0) {
		TEXT("statName", snapshot.last_race.rows[index].name);
	}
#undef TEXT

	return "";
}
