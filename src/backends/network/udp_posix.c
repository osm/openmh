#define _POSIX_C_SOURCE 200112L

#include <sys/socket.h>

#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/network/udp_provider.h"
#include "backends/network/udp_socket.h"

struct mh_udp_socket {
	struct mh_udp_wait	*wait;
	int			descriptor;
};

static void
native_address(const struct mh_udp_address *source, struct sockaddr_in *output)
{
	memset(output, 0, sizeof(*output));

	output->sin_family = AF_INET;
	output->sin_addr.s_addr = htonl(source->ipv4);
	output->sin_port = htons((unsigned short)source->port);
}

enum mh_error_code
mh_udp_socket_open(struct mh_udp_address *local, struct mh_udp_wait *wait,
	struct mh_udp_socket **output, struct mh_error *error_out)
{
	struct sockaddr_in bound;
	struct mh_udp_socket *result;
	socklen_t length;
	int fd, flags, system_error;
	enum mh_error_code status;

	native_address(local, &bound);
	fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (fd < 0)
		return mh_error_system(
			error_out, errno, "cannot create LAN/INET socket");

	flags = fcntl(fd, F_GETFL, 0);

	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
		fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ||
		bind(fd, (const struct sockaddr *)&bound, sizeof(bound)) < 0) {
		system_error = errno;
		close(fd);
		return mh_error_system(
			error_out, system_error, "cannot bind LAN/INET socket");
	}

	length = sizeof(bound);

	if (getsockname(fd, (struct sockaddr *)&bound, &length) < 0) {
		system_error = errno;
		close(fd);
		return mh_error_system(error_out, system_error,
			"cannot read LAN/INET socket address");
	}

	result = malloc(sizeof(*result));

	if (result == NULL) {
		close(fd);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate LAN/INET socket");
	}

	result->descriptor = fd;
	result->wait = wait;
	status = mh_udp_wait_add(wait, result, error_out);

	if (status != MH_ERROR_NONE) {
		mh_udp_socket_close(result);
		return status;
	}

	local->ipv4 = ntohl(bound.sin_addr.s_addr);
	local->port = ntohs(bound.sin_port);
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_udp_socket_close(struct mh_udp_socket *socket)
{
	if (socket == NULL)
		return;

	mh_udp_wait_remove(socket->wait, socket);
	close(socket->descriptor);
	free(socket);
}

enum mh_error_code
mh_udp_socket_send(struct mh_udp_socket *socket,
	const struct mh_udp_address *remote, const unsigned char *data,
	size_t size, struct mh_error *error_out)
{
	ssize_t count;
	struct sockaddr_in address;

	native_address(remote, &address);
	count = sendto(socket->descriptor, data, size, 0,
		(const struct sockaddr *)&address, sizeof(address));

	if (count < 0)
		return mh_error_system(
			error_out, errno, "cannot send LAN/INET datagram");

	if ((size_t)count != size)
		return mh_error_set(
			error_out, MH_ERROR_IO, "incomplete LAN/INET send");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_udp_socket_receive(struct mh_udp_socket *socket,
	struct mh_udp_address *remote, unsigned char *data, size_t capacity,
	size_t *size, int *received, struct mh_error *error_out)
{
	struct sockaddr_in address;
	socklen_t length;
	ssize_t count;

	length = sizeof(address);
	count = recvfrom(socket->descriptor, data, capacity, 0,
		(struct sockaddr *)&address, &length);

	if (count < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
			return mh_error_system(error_out, errno,
				"cannot receive LAN/INET datagram");

		*received = 0;
		*size = 0;
	} else {
		remote->ipv4 = ntohl(address.sin_addr.s_addr);
		remote->port = ntohs(address.sin_port);
		*received = 1;
		*size = (size_t)count;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_udp_wait_readable(const struct mh_udp_wait *wait, unsigned int milliseconds,
	struct mh_error *error_out)
{
	struct pollfd descriptors[MH_UDP_WAIT_SOCKETS];
	size_t i;
	int result;

	if (wait == NULL || wait->count > MH_UDP_WAIT_SOCKETS ||
		milliseconds > MH_UDP_WAIT_LIMIT_MS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid UDP wait");

	for (i = 0; i < wait->count; i++) {
		descriptors[i].fd = wait->sockets[i]->descriptor;
		descriptors[i].events = POLLIN;
		descriptors[i].revents = 0;
	}

	result = poll(descriptors, (nfds_t)wait->count, (int)milliseconds);

	/* Return to the host on signals so it can honor shutdown promptly. */
	if (result < 0 && errno != EINTR)
		return mh_error_system(
			error_out, errno, "cannot wait for UDP traffic");

	for (i = 0; i < wait->count && result > 0; i++)
		if ((descriptors[i].revents & POLLNVAL) != 0)
			return mh_error_set(error_out, MH_ERROR_IO,
				"invalid socket in UDP wait");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
