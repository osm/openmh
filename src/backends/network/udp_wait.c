#include "backends/network/udp_provider.h"
#include "backends/network/udp_socket.h"

enum mh_error_code
mh_udp_wait_add(struct mh_udp_wait *wait, struct mh_udp_socket *socket,
	struct mh_error *error_out)
{
	if (wait != NULL) {
		if (wait->count >= MH_UDP_WAIT_SOCKETS)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"UDP wait set is full");

		wait->sockets[wait->count++] = socket;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_udp_wait_remove(struct mh_udp_wait *wait, struct mh_udp_socket *socket)
{
	size_t i;

	if (wait == NULL)
		return;

	for (i = 0; i < wait->count; i++)
		if (wait->sockets[i] == socket) {
			wait->count--;
			wait->sockets[i] = wait->sockets[wait->count];
			wait->sockets[wait->count] = NULL;
			return;
		}
}
