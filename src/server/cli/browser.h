#ifndef MH_SERVER_CLI_BROWSER_H
#define MH_SERVER_CLI_BROWSER_H

#include "engine/core/error.h"

struct mh_server_arguments;
struct mh_server_host;
struct mh_transport;
struct mh_server_browser;
struct mh_udp_mux;

enum mh_error_code	mh_server_browser_validate(
	const struct mh_server_arguments *options, struct mh_error *error_out);

/*
 * Success transfers native transport into the browser owner; combined is
 * borrowed until browser close. Signaling borrows the prepared host. Failure
 * leaves native transport and both outputs unchanged. Poll only after host
 * init. The UDP dispatcher is borrowed through close.
 */
enum mh_error_code	mh_server_browser_open(
	const struct mh_server_arguments *options, struct mh_server_host *host,
	struct mh_transport *native, struct mh_udp_mux *mux,
	struct mh_server_browser **output, struct mh_transport **combined,
	struct mh_error *error_out);
enum mh_error_code	mh_server_browser_poll(
	struct mh_server_browser *browser, struct mh_error *error_out);
unsigned int		mh_server_browser_http_port(
	const struct mh_server_browser *browser);
unsigned int		mh_server_browser_udp_port(
	const struct mh_server_browser *browser);
void			mh_server_browser_close(
	struct mh_server_browser *browser);

#endif
