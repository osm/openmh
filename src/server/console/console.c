#include <stdio.h>
#include <string.h>

#include "engine/core/text.h"
#include "server/console/console.h"
#include "server/console/output.h"
#include "server/hosts/console.h"
#include "server/session/host.h"

static void
write_event(void *context, const char *text)
{
	(void)context;
	mh_server_console_puts(stdout, text);
}

void
mh_server_console_logger(struct mh_server_event_logger *logger)
{
	logger->write = write_event;
	logger->context = NULL;
}

void
mh_server_console_init(struct mh_server_console *console, int enabled)
{
	memset(console, 0, sizeof(*console));
	console->enabled = enabled;
}

static void
print_status(const struct mh_server_host *host)
{
	unsigned int i, players;

	players = 0;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++)
		if (host->lobby.roster.players[i].flags & MH_NET_FLAG_PRESENT)
			players++;

	mh_server_console_printf(stdout, "%s: %u/%lu drivers, %s, UDP %u\n",
		host->race.active ? "Race" : "Lobby", players,
		host->config->max_players, host->lobby.roster.track,
		mh_transport_port(host->transport));
}

static enum mh_error_code
execute_line(const char *line, struct mh_server_host *host, int *quit,
	struct mh_error *error_out)
{
	struct mh_server_operator_output output;
	unsigned int i;
	enum mh_error_code status;

	if (mh_ascii_equal(line, "status")) {
		print_status(host);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_server_host_command(host, line, &output, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < output.text.count; i++)
		mh_server_console_puts(stdout, output.text.lines[i]);

	*quit = output.action == MH_SERVER_OPERATOR_QUIT;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_console_poll(struct mh_server_console *console,
	struct mh_server_host *host, int *quit, struct mh_error *error_out)
{
	char bytes[MH_SERVER_CONSOLE_LINE_BYTES];
	size_t size, i;
	int eof;
	enum mh_error_code status;

	*quit = 0;

	if (!console->enabled)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_server_console_read(
		bytes, sizeof(bytes), &size, &eof, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (eof)
		console->enabled = 0;

	for (i = 0; i < size; i++) {
		if (bytes[i] == '\n' || bytes[i] == '\r') {
			console->line[console->used] = '\0';

			if (!console->discard) {
				status = execute_line(
					console->line, host, quit, error_out);

				if (status != MH_ERROR_NONE || *quit)
					return status;
			}

			console->used = 0;
			console->discard = 0;
		} else if (bytes[i] == '\b') {
			if (console->used != 0 && !console->discard)
				console->used--;
		} else if (console->used + 1 < sizeof(console->line))
			console->line[console->used++] = bytes[i];
		else
			console->discard = 1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
