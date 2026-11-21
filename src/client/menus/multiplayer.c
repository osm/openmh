#include <stdlib.h>

#include "client/menus/internal.h"
#include "engine/core/error.h"
#include "engine/core/simulation_rate.h"
#include "interfaces/network/address.h"
#include "interfaces/network/backend.h"
#include "interfaces/storage/data_directory.h"

/* CFG protocol selection and network provider use different original IDs. */
enum {
	TCP_IP_PROTOCOL = 1,
	BUILTIN_ADDRESSES = 1
};

/* Borrow only saved entries; the legacy catalog's search row is not usable. */
static struct mh_address_book
address_book(const struct mh_menus *menus)
{
	struct mh_address_book book;

	book = menus->address_store != NULL
		? *mh_address_store_book(menus->address_store)
		: menus->addresses;

	if (book.count != 0) {
		book.entries += BUILTIN_ADDRESSES;
		book.count -= BUILTIN_ADDRESSES;
	}

	return book;
}

static void
selected_address(
	const struct mh_menus *menus, char filename[MH_ADDRESS_FILENAME_BYTES])
{
	struct mh_address_book book;

	book = address_book(menus);
	filename[0] = '\0';

	if (menus->multi.address < book.count)
		strcpy(filename, book.entries[menus->multi.address].filename);
}

static void
restore_address(struct mh_menus *menus, const char *filename)
{
	struct mh_address_book book;
	unsigned int i;

	book = address_book(menus);

	for (i = 0; i < book.count; i++)
		if (strcmp(book.entries[i].filename, filename) == 0) {
			menus->multi.address = i;
			return;
		}

	/* Removed selections retain the menu's existing index/clamp policy. */
}

enum mh_error_code
mh_menus_multiplayer_addresses_reload(
	struct mh_menus *menus, struct mh_error *error_out)
{
	char filename[MH_ADDRESS_FILENAME_BYTES];
	enum mh_error_code status;

	if (menus->address_store == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	selected_address(menus, filename);
	status = mh_address_store_reload(menus->address_store, error_out);

	if (status == MH_ERROR_NONE)
		restore_address(menus, filename);

	return status;
}

static enum mh_error_code
prepare_address_directory(struct mh_menus *menus,
	char user_path[MH_MENUS_PATH_BYTES], struct mh_error *error_out)
{
	char *root;
	size_t length;
	enum mh_error_code status;

	status = mh_user_data_directory(menus->user_data, &root, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	length = strlen(root);

	if (length >= MH_MENUS_PATH_BYTES) {
		free(root);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"address book path too long");
	}

	memcpy(user_path, root, length);
	free(root);

	user_path[length] = 0;
	status = mh_user_data_create_directory(
		menus->user_data, user_path, error_out);

	return status;
}

enum mh_error_code
mh_menus_multiplayer_addresses_load(struct mh_menus *menus,
	const char *directory, int persist, struct mh_error *error_out)
{
	struct mh_data_directory *data;
	char user_path[MH_MENUS_PATH_BYTES];
	enum mh_error_code status;

	status = mh_address_book_read(NULL, 0, &menus->addresses, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (directory == NULL && persist && !menus->capture_only) {
		status = prepare_address_directory(menus, user_path, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		directory = user_path;
	}

	if (directory == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_user_data_open_directory(menus->user_data, directory,
		menus->asset_root, &data, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_address_store_open_in(
		data, &menus->address_store, error_out);

	if (status != MH_ERROR_NONE)
		mh_data_directory_close(data);

	return status;
}

static void
build_catalog(struct mh_menus *menus, struct mh_menu_multi_catalog *catalog)
{
	struct mh_address_book book;
	unsigned int i;

	book = address_book(menus);

	for (i = 0; i < book.count; i++) {
		menus->address_rows[i].name = book.entries[i].profile.name;
		menus->address_rows[i].ip = book.entries[i].profile.ip;
	}

	memset(catalog, 0, sizeof(*catalog));
	catalog->protected_count = 0;
	catalog->addresses = menus->address_rows;
	catalog->address_count = book.count;

	if (menus->debug.address_trace && menus->multi.address < book.count) {
		fprintf(stderr, "Address selection: %u/%u, name %s, IP %s\n",
			menus->multi.address, book.count,
			book.entries[menus->multi.address].profile.name,
			book.entries[menus->multi.address].profile.ip);
	}

	if (menus->debug.address_trace && book.count == 0)
		fputs("Address selection: empty\n", stderr);

	for (i = 0; i < menus->discovered_sessions.count; i++)
		menus->session_names[i] =
			menus->discovered_sessions.entries[i].game.name;

	catalog->sessions = menus->session_names;
	catalog->session_count = menus->discovered_sessions.count;
}

void
mh_menus_multiplayer_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->multi_entries++;

	/* Both network protocols use the TCP/IP address book. */
	menus->settings->protocol = TCP_IP_PROTOCOL;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_multi_enter(&menus->multi, &menus->error);

	mh_menu_multi_animation_init(&menus->multi_animation);
	printf("Multiplayer menu: addresses %u\n", address_book(menus).count);
	fflush(stdout);
}

void
mh_menus_multiplayer_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_multi_catalog catalog;

	menus = (struct mh_menus *)context;
	build_catalog(menus, &catalog);

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_multi_render(&menus->multi_art, &menus->font,
			&menus->layout, &menus->multi, menus->settings,
			&catalog, menu->position_mix, menus->clock_ms,
			&menus->multi_animation, &menus->random_state,
			&menus->row_noise, &menus->work, &menus->error);
}

void
mh_menus_multiplayer_leave(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	mh_transport_close(menus->discovery_transport);
	menus->discovery_transport = NULL;
}

static enum mh_error_code
begin_discovery(
	struct mh_menus *menus, unsigned int index, struct mh_error *error_out)
{
	struct mh_address_book book;
	enum mh_error_code status;

	book = address_book(menus);

	if (index >= book.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid discovery address selection");

	if (menus->network_wire == MH_NET_WIRE_31_LOBBY &&
		book.entries[index].profile.ip[0] == 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"LAN/INET needs a host address. Use Insert to add one.");

	memset(&menus->discovered_sessions, 0,
		sizeof(menus->discovered_sessions));
	mh_transport_close(menus->discovery_transport);
	menus->discovery_transport = NULL;
	status = mh_transport_open_address(menus->network, 0, NULL, 0,
		&menus->discovery_transport, error_out);
	menus->discovery_token = (menus->discovery_token + 1) & MH_U32_MAX;

	if (menus->discovery_token == 0)
		menus->discovery_token = 1;

	if (status == MH_ERROR_NONE)
		status = mh_transport_search_address(menus->discovery_transport,
			book.entries[index].profile.ip, menus->network_port,
			menus->discovery_token, error_out);

	if (status != MH_ERROR_NONE) {
		mh_transport_close(menus->discovery_transport);
		menus->discovery_transport = NULL;
	}

	return status;
}

static enum mh_error_code
queue_connection(struct mh_menus *menus, int host, const char *address,
	unsigned int port, const char *name, struct mh_error *error_out)
{
	struct mh_transport_target *target;
	enum mh_error_code status;

	if (!mh_menus_feature_enabled(
		&menus->configuration, MH_MENU_FEATURE_NETWORK) ||
		menus->network == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Network play is unavailable on this platform.");

	if (strlen(name) >= sizeof(menus->connection_name))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"network session name is too long");

	status = mh_transport_target_address(
		menus->network, address, port, &target, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_transport_target_close(menus->connection_target);
	menus->connection_target = target;
	menus->connection.host = host;
	menus->connection.local = host ? target : NULL;
	menus->connection.remote = host ? NULL : target;
	strcpy(menus->connection_name, name);
	menus->connection.name = menus->connection_name;
	menus->launch_mode = MH_GAME_NETWORK;
	mh_menus_request(&menus->menu, &menus->launch_page);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menus_multiplayer_connect(struct mh_menus *menus, const char *address,
	unsigned int port, struct mh_error *error_out)
{
	if (menus == NULL || address == NULL || address[0] == 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing server address");

	menus->settings->protocol = TCP_IP_PROTOCOL;

	return queue_connection(menus, 0, address, port, "OpenMH", error_out);
}

static void
request_connection(struct mh_menus *menus, int host, unsigned int index)
{
	struct mh_error diagnostic;
	const struct mh_discovery_entry *entry;
	const char *address, *name;
	unsigned int port;

	port = menus->network_port;
	address = NULL;
	name = menus->settings->session_name;

	if (!host) {
		if (index >= menus->discovered_sessions.count) {
			mh_error_set(&diagnostic, MH_ERROR_STATE,
				"The selected session is no longer available.");
			mh_menus_show_error(menus, diagnostic);
			return;
		}

		entry = &menus->discovered_sessions.entries[index];
		sprintf(menus->server_address, "%lu.%lu.%lu.%lu",
			(entry->ipv4 >> 24) & 255, (entry->ipv4 >> 16) & 255,
			(entry->ipv4 >> 8) & 255, entry->ipv4 & 255);
		address = menus->server_address;
		port = entry->port;
		name = entry->game.name;
	}

	if (queue_connection(menus, host, address, port, name, &diagnostic) !=
		MH_ERROR_NONE)
		mh_menus_show_error(menus, diagnostic);
}

static void
poll_discovery(struct mh_menus *menus, struct mh_menu *menu)
{
	struct mh_menu_multi_output output;
	struct mh_error error;
	enum mh_error_code status;
	int done;

	done = 0;
	status = mh_transport_search_poll(
		menus->discovery_transport, &done, &error);

	if (status != MH_ERROR_NONE || done) {
		if (status == MH_ERROR_NONE)
			menus->discovered_sessions =
				*mh_transport_search_results(
					menus->discovery_transport);

		mh_transport_close(menus->discovery_transport);
		menus->discovery_transport = NULL;
		(void)mh_menu_multi_search_complete(&menus->multi,
			menus->discovered_sessions.count, &menus->error);
		menus->keyboard_reset = 1;

		if (status != MH_ERROR_NONE)
			mh_menus_show_error(menus, error);

		printf("Multiplayer discovery: %u sessions\n",
			menus->discovered_sessions.count);
		fflush(stdout);
	} else {
		memset(&output, 0, sizeof(output));
		output.searching = 1;
		(void)mh_menu_multi_overlay(&menus->font, &menus->layout,
			&output, menu->position_mix, &menus->work,
			&menus->error);
	}
}

static enum mh_error_code
add_address(struct mh_menus *menus, struct mh_error *error_out)
{
	char filename[MH_ADDRESS_FILENAME_BYTES];
	enum mh_error_code status;

	if (menus->capture_only)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"Address editing is disabled during captures.");

	selected_address(menus, filename);

	if (menus->address_store != NULL)
		status = mh_address_store_add(menus->address_store,
			menus->multi.address_name, menus->multi.ip_address,
			error_out);
	else
		status = mh_address_book_add(&menus->addresses,
			menus->multi.address_name, menus->multi.ip_address,
			error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (menus->address_store == NULL)
		mh_address_book_sort(&menus->addresses, NULL);

	restore_address(menus, filename);
	printf("Multiplayer address added: %u entries\n",
		address_book(menus).count);
	fflush(stdout);

	return status;
}

static enum mh_error_code
remove_address(
	struct mh_menus *menus, unsigned int index, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (menus->capture_only)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"Address editing is disabled during captures.");

	if (menus->address_store != NULL)
		status = mh_address_store_remove(menus->address_store,
			index + BUILTIN_ADDRESSES, error_out);
	else
		status = mh_address_book_remove(&menus->addresses,
			index + BUILTIN_ADDRESSES, error_out);

	if (status == MH_ERROR_NONE) {
		printf("Multiplayer address deleted: %u entries\n",
			address_book(menus).count);
		fflush(stdout);
	}

	return status;
}

static enum mh_error_code
search_sessions(
	struct mh_menus *menus, unsigned int index, struct mh_error *error_out)
{
	if (menus->network == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Network play is unavailable on this platform.");

	if (menus->network->discovery == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Session discovery is unavailable on this platform.");

	if (menus->capture_only)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"Discovery is disabled during captures.");

	return begin_discovery(menus, index, error_out);
}

static void
apply_events(struct mh_menus *menus, struct mh_menu *menu,
	const struct mh_menu_multi_output *output)
{
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i;

	for (i = 0; i < output->count && menus->error.code == MH_ERROR_NONE;
		i++) {
		status = MH_ERROR_NONE;

		switch (output->events[i].kind) {
		case MH_MULTI_SOUND:
			mh_menus_sound_event(menus, output->events[i].value);
			break;

		case MH_MULTI_MAIN:
			mh_menus_request(menu, &menus->main_page);
			break;

		case MH_MULTI_CONFIRM_DELETE:
			mh_menus_open_dialog(
				menus, MH_MENU_CONFIRM_ADDRESS_DELETE);
			break;

		case MH_MULTI_ADD:
			status = add_address(menus, &error);
			break;

		case MH_MULTI_DELETE:
			status = remove_address(
				menus, output->events[i].value, &error);
			break;

		case MH_MULTI_FIND:
			status = search_sessions(
				menus, output->events[i].value, &error);

			if (status != MH_ERROR_NONE) {
				(void)mh_menu_multi_search_complete(
					&menus->multi, 0, &menus->error);
				menus->keyboard_reset = 1;
			}

			break;

		case MH_MULTI_HOST:
		case MH_MULTI_JOIN:
			if (menus->network == NULL)
				status = mh_error_set(&error,
					MH_ERROR_UNSUPPORTED,
					"Network play is unavailable on this platform.");
			else {
				request_connection(menus,
					output->events[i].kind == MH_MULTI_HOST,
					output->events[i].value);
			}

			break;

		default:
			mh_error_set(&menus->error, MH_ERROR_INVALID,
				"unknown multiplayer menu request");
			break;
		}

		if (status != MH_ERROR_NONE) {
			mh_menus_show_error(menus, error);
			break;
		}
	}
}

void
mh_menus_multiplayer_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_multi_output output;
	struct mh_address_book book;
	enum mh_error_code status;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	if (menus->discovery_transport != NULL) {
		poll_discovery(menus, menu);
		return;
	}

	book = address_book(menus);
	menus->multi_keys.clock_ms = menus->clock_ms;
	menus->multi_keys.address_count = book.count;
	menus->multi_keys.protected_count = 0;
	menus->multi_keys.session_count = menus->discovered_sessions.count;
	status = mh_menu_multi_update(&menus->multi, menus->settings,
		&menus->multi_keys, menus->font.advance, &output,
		&menus->error);

	if (status == MH_ERROR_NONE)
		status = mh_menu_multi_overlay(&menus->font, &menus->layout,
			&output, menu->position_mix, &menus->work,
			&menus->error);

	if (status != MH_ERROR_NONE)
		return;

	apply_events(menus, menu, &output);
}

static void
bind_network_catalog(struct mh_menus *menus)
{
	unsigned int i;

	for (i = 0; i < menus->car_catalog.count; i++)
		menus->network_cars[menus->car_numbers[i] - 1] =
			menus->car_titles[i];

	for (i = 0; i < menus->track_catalog.count; i++)
		menus->network_tracks[menus->track_numbers[i] - 1] =
			menus->track_titles[i];

	menus->network_catalog.cars.names = menus->network_cars;
	menus->network_catalog.cars.count = menus->car_catalog.count;
	menus->network_catalog.tracks.names = menus->network_tracks;
	menus->network_catalog.tracks.count = menus->track_catalog.count;
	menus->network_catalog.horns = &menus->horns;
}

enum mh_error_code
mh_menus_multiplayer_open(struct mh_menus *menus, const char *asset_root,
	struct mh_settings *settings, const struct mh_session_options *options,
	int *quit_requested, struct mh_activity *output,
	struct mh_error *error_out)
{
	struct mh_car car;
	struct mh_track track;
	struct mh_net_session_config *config;
	enum mh_error_code status;
	unsigned int i;

	config = &menus->connection.config;
	memset(config, 0, sizeof(*config));
	status = mh_car_config_load_from(options->catalog_source,
		menus->resources, asset_root, settings->car + 1, &car,
		error_out);

	if (status == MH_ERROR_NONE)
		status = mh_track_config_load_from(options->catalog_source,
			menus->resources, asset_root, settings->track + 1,
			&track, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (strlen(car.name) >= MH_NET_NAME_BYTES ||
		strlen(track.name) >= MH_NET_NAME_BYTES ||
		settings->horn >= menus->horns.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network car, course or horn selection");

	/*
	 * Copy the frontend's car prefix. The published course prefix bounds
	 * host /track and F1.
	 */
	config->wire = menus->network_wire;
	config->simulation_rate = options->network_legacy
		? 0
		: (options->simulation_rate ? options->simulation_rate
					: MH_SIM_RATE_DEFAULT);
	config->provider = config->wire == MH_NET_WIRE_31_LOBBY
		? MH_NET_PROVIDER_LANINET
		: MH_NET_PROVIDER_IP;
	config->car_count = menus->car_scene.catalog.unlocked;
	config->laps = settings->laps;
	menus->connection.track_limit = menus->race_scene.unlocked;
	config->grid_seed = menus->random_state;
	strcpy(config->track, track.name);

	if (menus->connection.host)
		strcpy(config->password, settings->session_password);

	strcpy(config->profile.name, settings->player_name);
	strcpy(config->profile.team, settings->team_name);
	strcpy(config->profile.car, car.name);
	strcpy(config->profile.horn, menus->horns.horns[settings->horn].name);
	config->profile.gear_mode = settings->automatic;
	config->profile.update_rate = settings->tcp_rate;
	config->prediction = settings->prediction;
	config->profile.control_type = (unsigned int)mh_control_type_update(
		menus->applied_controls.codes[MH_CONTROL_LEFT], 0);
	config->profile.values[MH_NET_PROFILE_COLOR] = settings->player_color;

	for (i = 0; i < MH_SETTINGS_CAR_COLORS; i++)
		config->profile.values[MH_NET_PROFILE_CAR_COLOR1 + i] =
			settings->car_colors[i];

	bind_network_catalog(menus);

	return mh_network_game_open(asset_root, &menus->connection, options,
		&menus->network_catalog, settings, &menus->random_state,
		&menus->audio, quit_requested, output, error_out);
}
