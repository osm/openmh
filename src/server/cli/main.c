#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/clock/system_provider.h"
#include "backends/navigation/provider.h"
#include "backends/network/posix_provider.h"
#include "backends/network/udp_provider.h"
#include "backends/network/udp_transport_provider.h"
#include "backends/network/windows_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/zip_provider.h"
#include "engine/assets/packages.h"
#include "engine/storage/asset_path.h"
#include "engine/storage/vfs.h"
#include "interfaces/network/address.h"
#include "server/cli/arguments.h"
#include "server/cli/browser.h"
#include "server/config/load.h"
#include "server/console/console.h"
#include "server/console/output.h"
#include "server/courses/courses.h"
#include "server/hosts/application.h"
#include "server/hosts/console.h"
#include "server/logging/file.h"
#include "server/session/host.h"

enum {
	ROOT_BYTES = 4096
};

/* Bound timer, signaling and console servicing even without game traffic. */
#define MAX_WAIT_MILLISECONDS 10

static enum mh_error_code
load_configuration(const struct mh_server_arguments *options,
	struct mh_server_config *config, size_t *line,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_server_config_load(mh_filesystem_resources(),
		options->config_path, config, line, error_out);

	if (status == MH_ERROR_NOT_FOUND && !options->config_explicit) {
		mh_server_config_defaults(config);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return status;
}

static int
resolve_course_root(
	const struct mh_server_arguments *options, char root[ROOT_BYTES])
{
	size_t size;

	if (options->data_root != NULL) {
		size = strlen(options->data_root);

		if (size >= ROOT_BYTES)
			return 0;

		memcpy(root, options->data_root, size + 1);

		return 1;
	}

	size = mh_path_directory_length(options->config_path);

	if (size == 0) {
		strcpy(root, ".");
		return 1;
	}

	if (size >= ROOT_BYTES)
		return 0;

	memcpy(root, options->config_path, size);
	root[size] = '\0';

	return 1;
}

static enum mh_error_code
run_host_loop(struct mh_server_host *host,
	const struct mh_server_arguments *options,
	struct mh_server_browser *browser, const struct mh_udp_wait *wait,
	const struct mh_udp_mux *mux, struct mh_error *error_out)
{
	const struct mh_server_config *config;
	struct mh_transport *transport;
	struct mh_server_console console;
	enum mh_error_code status;
	int announced, quit;

	config = host->config;
	transport = host->transport;
	status = MH_ERROR_NONE;
	mh_server_console_init(&console, !config->disable_io);
	announced = 0;
#ifndef MH_ENABLE_WEBRTC
	(void)browser;
#endif

	while (status == MH_ERROR_NONE && !mh_server_console_stopped()) {
#ifdef MH_ENABLE_WEBRTC
		status = mh_server_browser_poll(browser, error_out);

#endif
		if (status == MH_ERROR_NONE)
			status = mh_server_host_poll(host, error_out);

		if (!announced && mh_transport_port(transport) != 0
#ifdef MH_ENABLE_WEBRTC
			&& (browser == NULL ||
				mh_server_browser_http_port(browser) != 0)
#endif
		) {
			announced = 1;

			if (!config->disable_io) {
				mh_server_console_printf(stdout,
					"OpenMH Server %s\n", MH_BUILD_VERSION);
				mh_server_console_printf(stdout,
					"Listening: %s:%u\n",
					options->bind_address,
					mh_transport_port(transport));
				mh_server_console_printf(stdout,
					"Drivers: %lu\n", config->max_players);
				mh_server_console_printf(stdout, "Track: %s\n",
					host->lobby.roster.track);

				if (config->network_legacy) {
					mh_server_console_puts(stdout,
						"Protocol: legacy (original peers)");
					mh_server_console_puts(stdout,
						"Simulation: variable-frame physics");
				} else {
					mh_server_console_puts(
						stdout, "Protocol: OpenMH");
					mh_server_console_printf(stdout,
						"Simulation: %lu Hz\n",
						config->simulation_rate);
				}

#ifdef MH_ENABLE_WEBRTC
				mh_server_console_printf(stdout,
					"Connections: UDP%s\n",
					browser != NULL ? ", WebRTC" : "");

				if (browser != NULL)
					mh_server_console_printf(stdout,
						"Browser connections: UDP %s:%u; signaling HTTP %s:%u/\n",
						options->browser_address,
						mh_server_browser_udp_port(
							browser),
						options->browser_http_bind,
						mh_server_browser_http_port(
							browser));

#else
				mh_server_console_puts(
					stdout, "Connections: UDP");
#endif
				fflush(stdout);
			}
		}

		quit = 0;

		if (status == MH_ERROR_NONE)
			status = mh_server_console_poll(
				&console, host, &quit, error_out);

		if (quit)
			break;

		if (status == MH_ERROR_NONE && !mh_server_console_stopped() &&
			!mh_udp_mux_pending(mux))
			status = mh_udp_wait_readable(
				wait, MAX_WAIT_MILLISECONDS, error_out);
	}

	return status;
}

static enum mh_error_code
serve(const struct mh_server_arguments *options,
	const struct mh_server_config *config,
	const struct mh_server_courses *courses, struct mh_error *error_out)
{
	struct mh_udp_transport_config network;
	struct mh_udp_wait wait;
	struct mh_udp_mux *mux;
	struct mh_transport_provider provider;
	struct mh_transport_filter filter;
	struct mh_transport *transport;
	struct mh_server_host host;
	struct mh_server_browser *browser;
	struct mh_server_log_file log_file;
	struct mh_server_race_logger race_logger;
	struct mh_server_event_logger event_logger;
	enum mh_error_code status;

	mux = NULL;
	memset(&wait, 0, sizeof(wait));
	memset(&network, 0, sizeof(network));
	network.wait = &wait;
	network.clock = mh_system_clock_source();
#ifdef _WIN32
	network.resolver = mh_windows_resolver_provider();
#else
	network.resolver = mh_posix_resolver_provider();
#endif
	memcpy(network.name, config->session_name, sizeof(network.name));
	network.port_policy = config->network_legacy ? MH_UDP_PORTS_RANGE
						: MH_UDP_PORTS_SHARED;
	network.client_port_first = (unsigned int)config->clients_start_port;
	network.client_port_last = (unsigned int)config->clients_end_port;

	/*
	 * Polling starts only after host initialization; close precedes return.
	 */
	mh_server_bans_filter(&host.bans, &filter);
	network.filter = &filter;

#ifdef MH_ENABLE_WEBRTC
	if (options->browser_requested) {
		status = mh_udp_mux_create(&mux, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		network.mux = mux;
	}
#endif

	status = mh_udp_transport_init(&provider, &network, error_out);

	if (status != MH_ERROR_NONE) {
		mh_udp_mux_close(mux);
		return status;
	}

	status = mh_transport_open_address(&provider, 1, options->bind_address,
		(unsigned int)config->port, &transport, error_out);

	if (status != MH_ERROR_NONE) {
		mh_udp_mux_close(mux);
		return status;
	}

	browser = NULL;

#ifdef MH_ENABLE_WEBRTC
	if (options->browser_requested)
		status = mh_server_browser_open(options, &host, transport, mux,
			&browser, &transport, error_out);
#endif

	if (status == MH_ERROR_NONE)
		status = mh_server_host_init(&host, config,
			config->tracks[courses->selected].name, transport,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_server_host_attach_courses(
			&host, courses, error_out);

	mh_server_log_file_init(
		&log_file, config->race_log, config->disable_io, &race_logger);
	host.race_logger = &race_logger;
	mh_server_console_logger(&event_logger);
	host.event_logger = config->disable_io ? NULL : &event_logger;

	if (status == MH_ERROR_NONE)
		status = run_host_loop(
			&host, options, browser, &wait, mux, error_out);

#ifdef MH_ENABLE_WEBRTC
	if (browser != NULL)
		mh_server_browser_close(browser);
	else
#endif
		mh_transport_close(transport);

	mh_udp_mux_close(mux);

	return status;
}

int
mh_server_main(int argc, char *argv[])
{
	struct mh_server_arguments options;
	struct mh_server_config config;
	struct mh_server_courses courses;
	struct mh_vfs *assets;
	struct mh_error error;
	char root[ROOT_BYTES];
	size_t line;
	unsigned int failed_track;
	enum mh_error_code status;
	int parsed;

	parsed = mh_server_parse_arguments(argc, argv, &options);

	if (parsed <= 0)
		return parsed < 0 ? 2 : 0;

#ifndef MH_ENABLE_WEBRTC
	if (options.browser_requested) {
		mh_server_console_puts(stderr,
			"This server was built without WebRTC support.");
		return 2;
	}

#else
	if (options.browser_requested &&
		mh_server_browser_validate(&options, &error) != MH_ERROR_NONE) {
		mh_server_console_printf(
			stderr, "Browser networking: %s\n", error.message);
		return 2;
	}
#endif

	if (!resolve_course_root(&options, root)) {
		mh_server_console_puts(stderr, "Course root path is too long.");
		return 2;
	}

	status = load_configuration(&options, &config, &line, &error);

	if (status == MH_ERROR_NONE && options.browser_requested &&
		config.network_legacy)
		status = mh_error_set(&error, MH_ERROR_INVALID,
			"browser networking requires the OpenMH game protocol");

	if (status != MH_ERROR_NONE) {
		mh_server_console_printf(stderr, "%s:%lu: %s\n",
			options.config_path, (unsigned long)line,
			error.message);
		return 1;
	}

	assets = NULL;
	status = mh_content_mount(mh_filesystem_resources(),
		mh_zip_resource_provider(), root, &assets, &error);

	if (status == MH_ERROR_NONE)
		status = mh_server_courses_configure(
			mh_catalog_fallback_source(), mh_vfs_resources(assets),
			mh_vfs_root(assets), &config, &error);

	if (status == MH_ERROR_NONE)
		status = mh_server_driver_catalog_load(
			mh_catalog_fallback_source(), mh_horn_fallback_source(),
			mh_vfs_resources(assets), mh_vfs_root(assets), &config,
			&error);

	if (status != MH_ERROR_NONE) {
		if (options.data_root == NULL && status == MH_ERROR_NOT_FOUND) {
			mh_server_console_puts(stderr, "No game assets found.");
			mh_server_console_printf(stderr,
				"Use --assets PATH or place a game .ass package in \"%s\".\n",
				root);
		} else {
			mh_server_console_printf(stderr, "Assets \"%s\": %s\n",
				root, error.message);
			mh_server_console_puts(stderr,
				"Use --assets PATH to select a game directory or .ass package.");
		}

		mh_vfs_close(assets);

		return 1;
	}

	status = mh_server_config_validate(&config, &error);

	failed_track = config.track_count;

	if (status == MH_ERROR_NONE)
		status = mh_server_courses_load(mh_navigation_tbl_routes(),
			mh_vfs_resources(assets), mh_vfs_root(assets), &config,
			&courses, &failed_track, &error);

	mh_vfs_close(assets);

	if (status != MH_ERROR_NONE) {
		mh_server_console_printf(stderr, "Courses (%s): %s\n",
			failed_track < config.track_count
				? config.tracks[failed_track].path
				: config.start_track,
			error.message);
		return 1;
	}

	if (options.check_only)
		mh_server_console_printf(stdout,
			"Configuration valid: %u courses, start %s, %lu driver slots\n",
			courses.count, config.tracks[courses.selected].name,
			config.max_players);
	else {
		config.disable_io |= options.disable_io;
		status = mh_server_console_acquire(&error);

		if (status == MH_ERROR_NONE) {
			status = serve(&options, &config, &courses, &error);
			mh_server_console_release();
		}

		if (status != MH_ERROR_NONE)
			mh_server_console_printf(
				stderr, "Server: %s\n", error.message);
	}

	mh_server_courses_free(&courses);

	return status != MH_ERROR_NONE;
}
