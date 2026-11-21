#include <stdio.h>
#include <string.h>

#include "server/cli/arguments.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "server arguments:%d: %s\n", __LINE__, \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_server_arguments options;
	char *args[] = {"openmotorsv", "--config", "custom.cfg", "--assets",
		"game.ass", "--bind", "::1", "--check", "--no-console",
		"--config", "last.cfg", "--"};

	char *bad[][3] = {{"openmotorsv", "--config", "--check"},
		{"openmotorsv", "--bind", ""},
		{"openmotorsv", "--assets", "--help"},
		{"openmotorsv", "--", "--help"},
		{"openmotorsv", "--unknown", "value"},
		{"openmotorsv", "file", "--help"}};

	char *help[] = {"openmotorsv", "--check", "--help"};
	char *web[] = {"openmotorsv", "--web-address", "192.0.2.1",
		"--web-origin", "https://game.invalid", "--web-http-port",
		"1234", "--web-public-port", "5678", "--web-proxy",
		"127.0.0.1"};

	size_t i;

	CHECK(mh_server_parse_arguments(1, args, &options) == 1);
	CHECK(strcmp(options.config_path, "server.cfg") == 0 &&
		options.data_root == NULL && !options.config_explicit);
	CHECK(strcmp(options.bind_address, "127.0.0.1") == 0 &&
		!options.check_only && !options.disable_io);
	CHECK(!options.browser_requested && !options.browser_http_port_set &&
		options.browser_http_port == 0);
	CHECK(mh_server_parse_arguments((int)(sizeof(args) / sizeof(args[0])),
		args, &options) == 1);
	CHECK(strcmp(options.config_path, "last.cfg") == 0 &&
		strcmp(options.data_root, "game.ass") == 0 &&
		options.config_explicit);
	CHECK(strcmp(options.bind_address, "::1") == 0 && options.check_only &&
		options.disable_io);
	CHECK(mh_server_parse_arguments(2, args, &options) == -1);

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		CHECK(mh_server_parse_arguments(3, bad[i], &options) == -1);

	CHECK(mh_server_parse_arguments(3, help, &options) == 0);
	CHECK(mh_server_parse_arguments(
		(int)(sizeof(web) / sizeof(web[0])), web, &options) == 1);
	CHECK(options.browser_requested && options.browser_http_port_set &&
		options.browser_http_port == 1234 &&
		options.browser_public_port == 5678 &&
		strcmp(options.browser_proxy, "127.0.0.1") == 0);
	CHECK(mh_server_parse_arguments(3, web, &options) == -1);
	web[6] = "65536";
	CHECK(mh_server_parse_arguments(7, web, &options) == -1);
	web[6] = "-1";
	CHECK(mh_server_parse_arguments(7, web, &options) == -1);

	return 0;
}
