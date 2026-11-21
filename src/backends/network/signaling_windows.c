#define WIN32_LEAN_AND_MEAN
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "backends/network/signaling_socket.h"

#define SIGNAL_LISTEN_BACKLOG 16

/* Each socket owns one Winsock startup reference through close. */
struct mh_signal_socket {
	SOCKET	handle;
};

static enum mh_error_code
own_socket(SOCKET handle, struct mh_signal_socket **output,
	struct mh_error *error_out)
{
	struct mh_signal_socket *socket;

	u_long nonblocking;

	nonblocking = 1;

	if (ioctlsocket(handle, FIONBIO, &nonblocking) == SOCKET_ERROR ||
		!SetHandleInformation((HANDLE)handle, HANDLE_FLAG_INHERIT, 0)) {
		closesocket(handle);
		WSACleanup();
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot configure signaling socket");
	}

	socket = malloc(sizeof(*socket));

	if (socket == NULL) {
		closesocket(handle);
		WSACleanup();
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate signaling socket");
	}

	socket->handle = handle;
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
	WSADATA data;
	SOCKET handle;
	BOOL exclusive;
	int size;
	enum mh_error_code status;

	if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot initialize signaling Winsock");

	handle = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
		WSA_FLAG_NO_HANDLE_INHERIT);

	if (handle == INVALID_SOCKET) {
		WSACleanup();
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot create signaling listener");
	}

	status = own_socket(handle, &socket_owner, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(ipv4);
	address.sin_port = htons((unsigned short)port);
	exclusive = TRUE;
	size = sizeof(address);

	if (setsockopt(handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
		(const char *)&exclusive,
		sizeof(exclusive)) == SOCKET_ERROR ||
		bind(handle, (const struct sockaddr *)&address,
			sizeof(address)) == SOCKET_ERROR ||
		listen(handle, SIGNAL_LISTEN_BACKLOG) == SOCKET_ERROR ||
		getsockname(handle, (struct sockaddr *)&address, &size) ==
			SOCKET_ERROR) {
		mh_signal_socket_close(socket_owner);
		return mh_error_set(error_out, MH_ERROR_IO,
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
	WSADATA data;
	SOCKET handle;
	int size, number;
	enum mh_error_code status;

	size = sizeof(address);
	handle = accept(listener->handle, (struct sockaddr *)&address, &size);

	if (handle == INVALID_SOCKET) {
		number = WSAGetLastError();

		if (number != WSAEWOULDBLOCK && number != WSAEINTR &&
			number != WSAECONNRESET && number != WSAECONNABORTED)
			return mh_error_set(error_out, MH_ERROR_IO,
				"cannot accept signaling connection");

		*accepted = 0;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
		closesocket(handle);
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot retain signaling Winsock");
	}

	status = own_socket(handle, &socket, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&remote, 0, sizeof(remote));

	if (InetNtopA(AF_INET, &address.sin_addr, remote.host,
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
	int count, number;

	if (capacity > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"signaling read buffer is too large");

	count = recv(socket->handle, (char *)data, (int)capacity, 0);

	if (count == SOCKET_ERROR) {
		number = WSAGetLastError();

		if (number != WSAEWOULDBLOCK && number != WSAEINTR)
			return mh_error_set(error_out, MH_ERROR_IO,
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
	int count, number;

	if (size > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"signaling response is too large");

	count = send(socket->handle, (const char *)data, (int)size, 0);

	if (count == SOCKET_ERROR) {
		number = WSAGetLastError();

		if (number != WSAEWOULDBLOCK && number != WSAEINTR)
			return mh_error_set(error_out, MH_ERROR_IO,
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
		closesocket(socket->handle);
		WSACleanup();
		free(socket);
	}
}

int
mh_signal_address_normalize(const char *text, char *output, size_t capacity)
{
	struct in_addr ipv4;
	struct in6_addr ipv6;

	if (capacity > INT_MAX)
		return 0;

	if (InetPtonA(AF_INET, text, &ipv4) == 1)
		return InetNtopA(AF_INET, &ipv4, output, (DWORD)capacity) !=
			NULL;

	if (InetPtonA(AF_INET6, text, &ipv6) == 1)
		return InetNtopA(AF_INET6, &ipv6, output, (DWORD)capacity) !=
			NULL;

	return 0;
}
