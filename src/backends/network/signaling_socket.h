#ifndef MH_BACKENDS_SIGNALING_SOCKET_H
#define MH_BACKENDS_SIGNALING_SOCKET_H

#include "interfaces/network/network.h"

struct mh_signal_socket;

enum mh_error_code	mh_signal_socket_listen(unsigned long ipv4,
	unsigned int port, struct mh_signal_socket **output,
	unsigned int *bound_port, struct mh_error *error_out);
enum mh_error_code	mh_signal_socket_accept(
	struct mh_signal_socket *listener, struct mh_signal_socket **output,
	struct mh_transport_address *client, unsigned long *ipv4, int *accepted,
	struct mh_error *error_out);
enum mh_error_code	mh_signal_socket_read(struct mh_signal_socket *socket,
	unsigned char *data, size_t capacity, size_t *size, int *eof,
	struct mh_error *error_out);
enum mh_error_code	mh_signal_socket_write(struct mh_signal_socket *socket,
	const unsigned char *data, size_t size, size_t *written,
	struct mh_error *error_out);
void			mh_signal_socket_close(struct mh_signal_socket *socket);
int			mh_signal_address_normalize(const char *text,
	char *output, size_t capacity);

#endif
