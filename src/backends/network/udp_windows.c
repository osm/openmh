#define WIN32_LEAN_AND_MEAN
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#include "backends/network/udp_provider.h"
#include "backends/network/udp_socket.h"

/* Each owned socket retains one Winsock startup reference through close. */
struct mh_udp_socket {
	struct mh_udp_wait	*wait;
	SOCKET			handle;
};

static enum mh_error_code
socket_error(struct mh_error *error_out, int number, const char *message)
{
	enum mh_error_code code;

	switch (number) {
	case WSAEACCES:
		code = MH_ERROR_PERMISSION;
		break;

	case WSAEADDRINUSE:
		code = MH_ERROR_EXISTS;
		break;

	case WSA_NOT_ENOUGH_MEMORY:
	case WSAENOBUFS:
		code = MH_ERROR_MEMORY;
		break;

	case WSAEMSGSIZE:
		code = MH_ERROR_RANGE;
		break;

	default:
		code = MH_ERROR_IO;
		break;
	}

	return mh_error_set(error_out, code, message);
}

static void
native_address(const struct mh_udp_address *source, struct sockaddr_in *output)
{
	memset(output, 0, sizeof(*output));

	output->sin_family = AF_INET;
	output->sin_addr.s_addr = htonl(source->ipv4);
	output->sin_port = htons((unsigned short)source->port);
}

static enum mh_error_code
bind_socket(
	SOCKET handle, struct sockaddr_in *address, struct mh_error *error_out)
{
	u_long nonblocking;
	BOOL exclusive;
	int length;

	nonblocking = 1;
	exclusive = TRUE;

	if (ioctlsocket(handle, FIONBIO, &nonblocking) == SOCKET_ERROR ||
		setsockopt(handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
			(const char *)&exclusive,
			sizeof(exclusive)) == SOCKET_ERROR ||
		bind(handle, (const struct sockaddr *)address,
			sizeof(*address)) == SOCKET_ERROR)
		return socket_error(error_out, WSAGetLastError(),
			"cannot bind LAN/INET socket");

	length = sizeof(*address);

	if (getsockname(handle, (struct sockaddr *)address, &length) ==
		SOCKET_ERROR)
		return socket_error(error_out, WSAGetLastError(),
			"cannot read LAN/INET socket address");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_udp_socket_open(struct mh_udp_address *local, struct mh_udp_wait *wait,
	struct mh_udp_socket **output, struct mh_error *error_out)
{
	struct mh_udp_socket *result;
	struct sockaddr_in address;
	WSADATA data;
	SOCKET handle;
	int number;
	enum mh_error_code status;

	number = WSAStartup(MAKEWORD(2, 2), &data);

	if (number != 0)
		return socket_error(
			error_out, number, "cannot initialize Winsock");

	handle = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0,
		WSA_FLAG_NO_HANDLE_INHERIT);

	if (handle == INVALID_SOCKET) {
		number = WSAGetLastError();
		WSACleanup();
		return socket_error(
			error_out, number, "cannot create LAN/INET socket");
	}

	native_address(local, &address);
	status = bind_socket(handle, &address, error_out);

	if (status != MH_ERROR_NONE) {
		closesocket(handle);
		WSACleanup();
		return status;
	}

	result = malloc(sizeof(*result));

	if (result == NULL) {
		closesocket(handle);
		WSACleanup();
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate LAN/INET socket");
	}

	result->handle = handle;
	result->wait = wait;
	status = mh_udp_wait_add(wait, result, error_out);

	if (status != MH_ERROR_NONE) {
		mh_udp_socket_close(result);
		return status;
	}

	local->ipv4 = ntohl(address.sin_addr.s_addr);
	local->port = ntohs(address.sin_port);
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_udp_socket_close(struct mh_udp_socket *socket)
{
	if (socket == NULL)
		return;

	mh_udp_wait_remove(socket->wait, socket);
	closesocket(socket->handle);
	WSACleanup();
	free(socket);
}

enum mh_error_code
mh_udp_socket_send(struct mh_udp_socket *socket,
	const struct mh_udp_address *remote, const unsigned char *data,
	size_t size, struct mh_error *error_out)
{
	struct sockaddr_in address;
	int count;

	if (size > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"LAN/INET datagram exceeds socket limit");

	native_address(remote, &address);
	count = sendto(socket->handle, (const char *)data, (int)size, 0,
		(const struct sockaddr *)&address, sizeof(address));

	if (count == SOCKET_ERROR)
		return socket_error(error_out, WSAGetLastError(),
			"cannot send LAN/INET datagram");

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
	int count, length, number;

	if (capacity > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"LAN/INET receive buffer exceeds socket limit");

	length = sizeof(address);
	count = recvfrom(socket->handle, (char *)data, (int)capacity, 0,
		(struct sockaddr *)&address, &length);

	if (count == SOCKET_ERROR) {
		number = WSAGetLastError();

		/* recvfrom has consumed the datagram and filled the buffer. */
		if (number == WSAEMSGSIZE)
			count = (int)capacity;
		else if (number == WSAEWOULDBLOCK || number == WSAEINTR ||
			number == WSAECONNRESET) {
			/*
			 * UDP port-unreachable reports leave retry policy to
			 * the transport.
			 */
			*size = 0;
			*received = 0;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		} else
			return socket_error(error_out, number,
				"cannot receive LAN/INET datagram");
	}

	remote->ipv4 = ntohl(address.sin_addr.s_addr);
	remote->port = ntohs(address.sin_port);
	*size = (size_t)count;
	*received = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_udp_wait_readable(const struct mh_udp_wait *wait, unsigned int milliseconds,
	struct mh_error *error_out)
{
	fd_set readable;
	struct timeval timeout;
	size_t i;
	int result, number;

	if (wait == NULL || wait->count > MH_UDP_WAIT_SOCKETS ||
		wait->count > FD_SETSIZE || milliseconds > MH_UDP_WAIT_LIMIT_MS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid UDP wait");

	/*
	 * Winsock select rejects empty sets, including before the first bind.
	 */
	if (wait->count == 0) {
		Sleep(milliseconds);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	FD_ZERO(&readable);

	for (i = 0; i < wait->count; i++)
		FD_SET(wait->sockets[i]->handle, &readable);

	timeout.tv_sec = (long)(milliseconds / 1000);
	timeout.tv_usec = (long)(milliseconds % 1000) * 1000;
	result = select(0, &readable, NULL, NULL, &timeout);

	if (result == SOCKET_ERROR) {
		number = WSAGetLastError();

		if (number != WSAEINTR)
			return socket_error(error_out, number,
				"cannot wait for UDP traffic");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
