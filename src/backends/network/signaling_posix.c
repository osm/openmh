#define _POSIX_C_SOURCE 200112L

#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/network/signaling_socket.h"

#define SIGNAL_LISTEN_BACKLOG 16

struct mh_signal_socket {
	int	descriptor;
};

static enum mh_error_code
own_socket(int fd, struct mh_signal_socket **output, struct mh_error *error_out)
{
	struct mh_signal_socket *socket;
	int flags, number;

	flags = fcntl(fd, F_GETFL, 0);

	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
		fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
		number = errno;
		close(fd);
		return mh_error_system(
			error_out, number, "cannot configure signaling socket");
	}

	socket = malloc(sizeof(*socket));

	if (socket == NULL) {
		close(fd);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate signaling socket");
	}

	socket->descriptor = fd;
	*output = socket;

	return MH_ERROR_NONE;
}

enum mh_error_code
mh_signal_socket_listen(unsigned long ipv4, unsigned int port,
	struct mh_signal_socket **output, unsigned int *bound_port,
	struct mh_error *error_out)
{
	struct sockaddr_in address;
	struct mh_signal_socket *socket_owner;
	socklen_t size;
	int fd, reuse, number;
	enum mh_error_code status;

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(ipv4);
	address.sin_port = htons((unsigned short)port);
	fd = socket(AF_INET, SOCK_STREAM, 0);

	if (fd < 0)
		return mh_error_system(
			error_out, errno, "cannot create signaling listener");

	status = own_socket(fd, &socket_owner, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	reuse = 1;
	size = sizeof(address);

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
			0 ||
		bind(fd, (const struct sockaddr *)&address, sizeof(address)) <
			0 ||
		listen(fd, SIGNAL_LISTEN_BACKLOG) < 0 ||
		getsockname(fd, (struct sockaddr *)&address, &size) < 0) {
		number = errno;
		mh_signal_socket_close(socket_owner);
		return mh_error_system(error_out, number,
			"cannot listen for signaling requests");
	}

	*output = socket_owner;
	*bound_port = ntohs(address.sin_port);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_signal_socket_accept(struct mh_signal_socket *listener,
	struct mh_signal_socket **output, struct mh_transport_address *client,
	unsigned long *ipv4, int *accepted, struct mh_error *error_out)
{
	struct sockaddr_in address;
	struct mh_signal_socket *socket;
	struct mh_transport_address remote;
	socklen_t size;
	int fd;
	enum mh_error_code status;

	size = sizeof(address);
	fd = accept(listener->descriptor, (struct sockaddr *)&address, &size);

	if (fd < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR &&
			errno != ECONNABORTED)
			return mh_error_system(error_out, errno,
				"cannot accept signaling connection");

		*accepted = 0;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = own_socket(fd, &socket, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&remote, 0, sizeof(remote));

	if (inet_ntop(AF_INET, &address.sin_addr, remote.host,
		sizeof(remote.host)) == NULL) {
		mh_signal_socket_close(socket);
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot format signaling client address");
	}

	remote.port = ntohs(address.sin_port);
	*client = remote;
	*ipv4 = ntohl(address.sin_addr.s_addr);
	*output = socket;
	*accepted = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_signal_socket_read(struct mh_signal_socket *socket, unsigned char *data,
	size_t capacity, size_t *size, int *eof, struct mh_error *error_out)
{
	ssize_t count;

	count = recv(socket->descriptor, data, capacity, 0);

	if (count < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
			return mh_error_system(error_out, errno,
				"cannot read signaling request");

		*size = 0;
		*eof = 0;
	} else {
		*size = (size_t)count;
		*eof = count == 0;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_signal_socket_write(struct mh_signal_socket *socket,
	const unsigned char *data, size_t size, size_t *written,
	struct mh_error *error_out)
{
	ssize_t count;

	count = send(socket->descriptor, data, size, MSG_NOSIGNAL);

	if (count < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
			return mh_error_system(error_out, errno,
				"cannot write signaling response");

		count = 0;
	}

	*written = (size_t)count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_signal_socket_close(struct mh_signal_socket *socket)
{
	if (socket != NULL) {
		close(socket->descriptor);
		free(socket);
	}
}

int
mh_signal_address_normalize(const char *text, char *output, size_t capacity)
{
	struct in_addr ipv4;
	struct in6_addr ipv6;

	if (inet_pton(AF_INET, text, &ipv4) == 1)
		return inet_ntop(AF_INET, &ipv4, output, (socklen_t)capacity) !=
			NULL;

	if (inet_pton(AF_INET6, text, &ipv6) == 1)
		return inet_ntop(AF_INET6, &ipv6, output,
			(socklen_t)capacity) != NULL;

	return 0;
}
