#ifndef MH_SERVER_CLI_ARGUMENTS_H
#define MH_SERVER_CLI_ARGUMENTS_H

struct mh_server_arguments {
	const char	*config_path, *data_root, *bind_address;
	int		check_only, disable_io, config_explicit;
	const char *browser_address, *browser_origin, *browser_http_bind,
		*browser_proxy;
	unsigned int	browser_public_port, browser_http_port;
	int		browser_requested, browser_http_port_set;
};

/* Borrow argv strings; 1 runs the server, 0 is help, -1 is a usage error. */
int	mh_server_parse_arguments(int argc, char *argv[],
	struct mh_server_arguments *options);

#endif
