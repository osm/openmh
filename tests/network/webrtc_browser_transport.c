#include <stdio.h>
#include <string.h>

#include "backends/network/browser_provider.h"
#include "interfaces/network/backend.h"

enum {
	CLIENTS = 2,
	EXCHANGES = 4,
	GAME_MESSAGE_BYTES = 8191
};

static struct mh_transport *transports[CLIENTS];
static unsigned int progress[CLIENTS];
static unsigned long peers[CLIENTS];

void	mh_browser_transport_check_close(void);
int	mh_browser_transport_check_start(const char *first, const char *second);
int	mh_browser_transport_check_step(void);

static size_t
message(unsigned int index, unsigned int step, unsigned char *output)
{
	size_t size, i;

	size = step == 0 ? 1 : (step == 1 ? GAME_MESSAGE_BYTES : 2);

	for (i = 0; i < size; i++)
		output[i] = (unsigned char)((i * 17 + index + step) & 255);

	return size;
}

static int
send_next(unsigned int index)
{
	unsigned char data[GAME_MESSAGE_BYTES];
	size_t size;

	if (progress[index] == EXCHANGES) {
		data[0] = (unsigned char)index;
		data[1] = 0xdd;
		size = 2;
	} else
		size = message(index, progress[index], data);

	return mh_transport_send(transports[index], peers[index], data, size,
		progress[index] >= 2 && progress[index] < EXCHANGES,
		NULL) == MH_ERROR_NONE &&
		mh_transport_flush(transports[index], NULL) == MH_ERROR_NONE;
}

int
mh_browser_transport_check_start(const char *first, const char *second)
{
	struct mh_browser_network_config config;
	struct mh_transport_target *target;
	struct mh_transport *canceled;
	unsigned int i;

	memset(&config, 0, sizeof(config));

	for (i = 0; i < CLIENTS; i++) {
		config.signaling_url = i == 0 ? first : second;

		if (mh_browser_network_target(&config, &target, NULL) !=
			MH_ERROR_NONE)
			return 0;

		if (mh_transport_open_from(mh_browser_network_provider(), 0,
			NULL, &transports[i], NULL) != MH_ERROR_NONE) {
			mh_transport_target_close(target);
			return 0;
		}

		if (mh_transport_connect(transports[i], target, NULL) !=
			MH_ERROR_NONE) {
			mh_transport_target_close(target);
			return 0;
		}

		/*
		 * Closing a pending connection must cancel asynchronous
		 * signaling.
		 */
		canceled = NULL;

		if (mh_transport_open_from(mh_browser_network_provider(), 0,
			NULL, &canceled, NULL) != MH_ERROR_NONE ||
			mh_transport_connect(canceled, target, NULL) !=
				MH_ERROR_NONE) {
			mh_transport_close(canceled);
			mh_transport_target_close(target);
			return 0;
		}

		mh_transport_close(canceled);
		mh_transport_target_close(target);
	}

	return 1;
}

int
mh_browser_transport_check_step(void)
{
	struct mh_transport_event event;
	unsigned char expected[GAME_MESSAGE_BYTES];
	size_t size;
	unsigned int i, done;

	done = 0;

	for (i = 0; i < CLIENTS; i++) {
		if (progress[i] == EXCHANGES) {
			done++;
			continue;
		}

		if (mh_transport_poll(transports[i], 0, &event, NULL) !=
			MH_ERROR_NONE)
			return -1;

		if (event.type == MH_TRANSPORT_CONNECT) {
			if (peers[i] != 0)
				return -1;

			peers[i] = event.peer;

			if (!send_next(i))
				return -1;
		} else if (event.type == MH_TRANSPORT_RECEIVE) {
			size = message(i, progress[i], expected);

			if (event.peer != peers[i] || event.size != size ||
				event.reliable != (progress[i] >= 2) ||
				memcmp(event.data, expected, size) != 0)
				return -1;

			progress[i]++;

			if (!send_next(i))
				return -1;
		} else if (event.type != MH_TRANSPORT_NONE) {
			fprintf(stderr, "Unexpected browser event %d: %s\n",
				(int)event.type,
				event.size ? (char *)event.data : "");
			return -1;
		}
	}

	return done == CLIENTS ? 1 : 0;
}

void
mh_browser_transport_check_close(void)
{
	unsigned int i;

	for (i = 0; i < CLIENTS; i++) {
		mh_transport_close(transports[i]);
		transports[i] = NULL;
	}
}

int
main(void)
{
	return 0;
}
