#include <stdio.h>
#include <string.h>

#include "engine/core/arguments.h"
#include "engine/core/limits.h"
#include "server/cli/arguments.h"
#include "server/config/config.h"
#include "server/console/output.h"

static void
usage(void)
{
	static const char *const lines[] = {
		("OpenMH dedicated server " MH_BUILD_VERSION), "",
		"Usage: openmotorsv [options]", "", "Options",
		"  --help                           Show this help and exit.",
		"  --no-console                     Disable console input and output.",
		"", "Game and resources",
		"  --assets PATH                    Game directory or .ass package (default:",
		"                                   configuration directory).",
		"  --config FILE                    Server configuration (default: server.cfg if",
		"                                   present).",
		"  --check                          Validate settings and courses without",
		"                                   starting networking.", "",
		"Networking",
		"  --bind ADDRESS                   Listening address (default: 127.0.0.1). Use",
		"                                   0.0.0.0 to accept remote connections.",
#ifdef MH_ENABLE_WEBRTC
		"", "Browser networking",
		"  --web-address IPV4               Reachable advertised UDP address.",
		"  --web-origin ORIGIN              Allowed origin, e.g.",
		"                                   https://play.example.org.",
		"  --web-public-port PORT           Advertised NAT port (default: bound UDP",
		"                                   port).",
		"  --web-http-bind IPV4             HTTP signaling bind address (default:",
		"                                   127.0.0.1).",
		"  --web-http-port PORT             HTTP signaling port (default: game port). Use",
		"                                   0 to choose a free port.",
		"  --web-proxy IPV4                 Trust X-Real-IP only from this reverse proxy.",
#endif
		"", "Defaults"};

	static const char *const notes[] = {
		"  Discover tracks from the game assets when no Track entries are configured.",
		"  Select the first track when StartTrack is not configured.",
		"", "Notes",
		"  An explicitly selected configuration file must exist.",
#ifdef MH_ENABLE_WEBRTC
		"  Browser and native gameplay share the game UDP port and --bind address.",
		"  Browser networking requires both --web-address and --web-origin.",
		"  Serve signaling through an HTTPS reverse proxy in production.",
#endif
		"", "Examples", "  openmotorsv",
		"  openmotorsv --assets game.ass --bind 0.0.0.0",
		"  openmotorsv --config server.cfg --assets game.ass --check",
		""};

	struct mh_server_config config;
	size_t i;

	mh_server_config_defaults(&config);

	for (i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
		puts(lines[i]);

	if (config.network_legacy)
		puts("  Legacy protocol, original simulation timing.");
	else
		printf("  OpenMH protocol, %lu Hz simulation.\n",
			config.simulation_rate);

	printf("  UDP port %lu, %lu driver slots, Normal mode and %lu laps.\n",
		config.port, config.max_players, config.laps);
	printf("  Ready players required to start: %lu.\n", config.min_ready);

	for (i = 0; i < sizeof(notes) / sizeof(notes[0]); i++)
		puts(notes[i]);
}

static enum mh_argument_kind
argument_kind(const char *name)
{
	static const struct mh_argument_option options[] = {
		{"--config", MH_ARGUMENT_VALUE},
		{"--assets", MH_ARGUMENT_VALUE}, {"--bind", MH_ARGUMENT_VALUE},
		{"--check", MH_ARGUMENT_FLAG},
		{"--web-address", MH_ARGUMENT_VALUE},
		{"--web-origin", MH_ARGUMENT_VALUE},
		{"--web-public-port", MH_ARGUMENT_VALUE},
		{"--web-http-bind", MH_ARGUMENT_VALUE},
		{"--web-http-port", MH_ARGUMENT_VALUE},
		{"--web-proxy", MH_ARGUMENT_VALUE},
		{"--no-console", MH_ARGUMENT_FLAG},
		{"--help", MH_ARGUMENT_FLAG}, {NULL, MH_ARGUMENT_UNKNOWN}};

	return mh_arguments_find(name, options);
}

int
mh_server_parse_arguments(
	int argc, char *argv[], struct mh_server_arguments *options)
{
	struct mh_arguments arguments;
	enum mh_argument_result result;
	const char *name;
	unsigned long port;

	memset(options, 0, sizeof(*options));
	options->config_path = "server.cfg";
	options->bind_address = "127.0.0.1";
	options->browser_http_bind = "127.0.0.1";
	mh_arguments_init(&arguments, argc, argv);

	while ((result = mh_arguments_next(&arguments, argument_kind)) !=
		MH_ARGUMENT_END) {
		name = arguments.argument;

		if (result == MH_ARGUMENT_ERROR ||
			result == MH_ARGUMENT_POSITIONAL) {
			mh_server_console_printf(stderr,
				"openmotorsv: %s: %s\n",
				result == MH_ARGUMENT_ERROR
					? arguments.message
					: "unexpected positional argument",
				name == NULL ? "" : name);
			return -1;
		}

		if (strcmp(name, "--help") == 0) {
			usage();
			return 0;
		}

		if (strcmp(name, "--check") == 0)
			options->check_only = 1;
		else if (strcmp(name, "--no-console") == 0)
			options->disable_io = 1;
		else if (strcmp(name, "--config") == 0) {
			options->config_path = arguments.value;
			options->config_explicit = 1;
		} else if (strcmp(name, "--assets") == 0)
			options->data_root = arguments.value;
		else if (strcmp(name, "--bind") == 0)
			options->bind_address = arguments.value;
		else if (strcmp(name, "--web-address") == 0)
			options->browser_address = arguments.value;
		else if (strcmp(name, "--web-origin") == 0)
			options->browser_origin = arguments.value;
		else if (strcmp(name, "--web-http-bind") == 0)
			options->browser_http_bind = arguments.value;
		else if (strcmp(name, "--web-proxy") == 0)
			options->browser_proxy = arguments.value;
		else if (strcmp(name, "--web-http-port") == 0 ||
			strcmp(name, "--web-public-port") == 0) {
			if (!mh_arguments_unsigned(
				arguments.value, MH_U16_MAX, &port)) {
				mh_server_console_printf(stderr,
					"openmotorsv: invalid port for %s\n",
					name);
				return -1;
			}

			if (strcmp(name, "--web-http-port") == 0) {
				options->browser_http_port = (unsigned int)port;
				options->browser_http_port_set = 1;
			} else
				options->browser_public_port =
					(unsigned int)port;
		}

		if (strncmp(name, "--web-", sizeof("--web-") - 1) == 0)
			options->browser_requested = 1;
	}

	if (options->browser_requested &&
		(options->browser_address == NULL ||
			options->browser_origin == NULL)) {
		mh_server_console_puts(stderr,
			"openmotorsv: browser networking requires --web-address and --web-origin");
		return -1;
	}

	return 1;
}
