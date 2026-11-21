#include <stdlib.h>
#include <string.h>

#include "backends/clock/system_provider.h"
#include "backends/network/combined_provider.h"
#include "backends/network/signaling_provider.h"
#include "backends/network/webrtc_provider.h"
#include "server/cli/arguments.h"
#include "server/cli/browser.h"
#include "server/session/host.h"

#define BROWSER_IPV4_FIRST_UNICAST 0x01000000UL
#define BROWSER_IPV4_FIRST_MULTICAST 0xe0000000UL

struct mh_server_browser {
	struct mh_webrtc_server		*endpoint;
	struct mh_signaling_server	*signaling;
	struct mh_transport		*transport, *native;
	struct mh_signaling_config	signal;
	int				http_port_set;
	struct mh_server_host		*host;
	struct mh_transport_filter	filter, bans;
	unsigned int			udp_port;
};

/* Configuration accepts numeric IPv4 only; no blocking name lookup. */
static int
ipv4_address(const char *text, unsigned long *output)
{
	unsigned long address;
	unsigned int octet, value, digits;

	if (text == NULL)
		return 0;

	address = 0;

	for (octet = 0; octet < 4; octet++) {
		value = digits = 0;

		while (*text >= '0' && *text <= '9') {
			if (++digits > 3 || (digits > 1 && value == 0))
				return 0;

			value = value * 10 + (unsigned int)(*text++ - '0');
		}

		if (digits == 0 || value > 255 ||
			(octet < 3 ? *text != '.' : *text != 0))
			return 0;

		address = (address << 8) | value;

		if (octet < 3)
			text++;
	}

	*output = address;

	return 1;
}

static enum mh_error_code
prepare_options(const struct mh_server_arguments *options,
	struct mh_webrtc_server_config *rtc, struct mh_signaling_config *signal,
	struct mh_error *error_out)
{
	memset(rtc, 0, sizeof(*rtc));

	memset(signal, 0, sizeof(*signal));

	if (!ipv4_address(options->browser_address, &rtc->advertised_ipv4) ||
		!ipv4_address(options->browser_http_bind, &signal->bind_ipv4) ||
		(options->browser_proxy != NULL &&
			!ipv4_address(
				options->browser_proxy, &signal->proxy_ipv4)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"browser server addresses must be numeric IPv4");

	if (rtc->advertised_ipv4 < BROWSER_IPV4_FIRST_UNICAST ||
		rtc->advertised_ipv4 >= BROWSER_IPV4_FIRST_MULTICAST)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"browser advertised address must be unicast IPv4");

	if (!mh_signaling_origin_valid(options->browser_origin))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"browser origin must be one http(s) origin without a path");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_browser_validate(
	const struct mh_server_arguments *options, struct mh_error *error_out)
{
	struct mh_webrtc_server_config rtc;
	struct mh_signaling_config signal;

	return prepare_options(options, &rtc, &signal, error_out);
}

static int
accept_browser(
	void *context, const struct mh_transport_address *address, double now)
{
	struct mh_server_browser *browser;

	(void)now;
	browser = context;

	/*
	 * Both providers evaluate bans against the host's primary clock epoch.
	 */

	return browser->bans.accept(browser->bans.context, address,
		mh_transport_now(browser->host->transport));
}

static enum mh_error_code
offer(void *context, const unsigned char *data, size_t size,
	const struct mh_transport_address *address, char *answer,
	size_t capacity, size_t *written, unsigned long *peer,
	struct mh_error *error_out)
{
	struct mh_server_browser *browser;

	browser = context;

	return mh_webrtc_server_offer(browser->endpoint, data, size, address,
		answer, capacity, written, peer, error_out);
}

static void
cancel(void *context, unsigned long peer)
{
	struct mh_server_browser *browser;

	browser = context;
	mh_webrtc_server_cancel(browser->endpoint, peer);
}

void
mh_server_browser_close(struct mh_server_browser *browser)
{
	if (browser != NULL) {
		mh_signaling_close(browser->signaling);
		mh_transport_close(browser->transport);
		free(browser);
	}
}

enum mh_error_code
mh_server_browser_open(const struct mh_server_arguments *options,
	struct mh_server_host *host, struct mh_transport *native,
	struct mh_udp_mux *mux, struct mh_server_browser **output,
	struct mh_transport **combined, struct mh_error *error_out)
{
	struct mh_webrtc_server_config rtc;
	struct mh_signaling_config signal;
	struct mh_server_browser *browser;
	struct mh_transport *transport;
	enum mh_error_code status;

	status = prepare_options(options, &rtc, &signal, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	browser = calloc(1, sizeof(*browser));

	if (browser == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate browser server host");

	browser->host = host;
	mh_server_bans_filter(&host->bans, &browser->bans);
	browser->filter = browser->bans;
	browser->filter.accept = accept_browser;
	browser->filter.context = browser;
	rtc.clock = mh_system_clock_source();
	rtc.mux = mux;
	rtc.filter = &browser->filter;
	rtc.advertised_port = options->browser_public_port;
	transport = NULL;
	status = mh_webrtc_server_open(
		&rtc, &browser->endpoint, &transport, error_out);

	if (status == MH_ERROR_NONE) {
		browser->udp_port = options->browser_public_port;

		signal.clock = mh_system_clock_source();
		signal.port = options->browser_http_port;
		signal.trust_proxy = options->browser_proxy != NULL;
		signal.origin = options->browser_origin;
		signal.context = browser;
		signal.offer = offer;
		signal.cancel = cancel;
		browser->signal = signal;
		browser->http_port_set = options->browser_http_port_set;
		browser->native = native;
	}

	if (status == MH_ERROR_NONE)
		status = mh_combined_transport_open(
			native, transport, combined, error_out);

	if (status != MH_ERROR_NONE) {
		mh_signaling_close(browser->signaling);
		mh_transport_close(transport);
		free(browser);
		return status;
	}

	browser->transport = *combined;
	*output = browser;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_browser_poll(
	struct mh_server_browser *browser, struct mh_error *error_out)
{
	unsigned int port;
	enum mh_error_code status;

	if (browser == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (browser->signaling == NULL) {
		port = mh_transport_port(browser->native);

		/*
		 * Native address resolution/binding can complete
		 * asynchronously.
		 */
		if (port == 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (!browser->http_port_set)
			browser->signal.port = port;

		status = mh_signaling_open(
			&browser->signal, &browser->signaling, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_signaling_poll(browser->signaling, error_out);
}

unsigned int
mh_server_browser_http_port(const struct mh_server_browser *browser)
{
	return browser == NULL ? 0 : mh_signaling_port(browser->signaling);
}

unsigned int
mh_server_browser_udp_port(const struct mh_server_browser *browser)
{
	if (browser == NULL)
		return 0;

	return browser->udp_port != 0 ? browser->udp_port
				: mh_transport_port(browser->native);
}
