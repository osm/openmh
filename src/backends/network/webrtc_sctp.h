#ifndef MH_WEBRTC_SCTP_H
#define MH_WEBRTC_SCTP_H

#include "engine/core/error.h"

#define MH_SCTP_MESSAGE_BYTES 8192
#define MH_SCTP_DATAGRAM_BYTES 1100
#define MH_SCTP_COOKIE_BYTES 32

struct mh_sctp;

struct mh_sctp_config {
	unsigned int	local_port, remote_port;
	unsigned long	tag, initial_tsn;
	unsigned char	cookie[MH_SCTP_COOKIE_BYTES];
};

struct mh_sctp_datagram {
	size_t		size;
	unsigned char	data[MH_SCTP_DATAGRAM_BYTES];
};

struct mh_sctp_message {
	size_t		size;
	unsigned char	data[MH_SCTP_MESSAGE_BYTES];
};

/*
 * Passive, single-path SCTP association for negotiated WebRTC channel 0.
 * Only unordered binary messages with zero retransmissions are supported.
 * Feed authenticated DTLS records, never raw UDP. Config includes a fresh
 * nonzero verification tag, initial TSN and unpredictable per-peer cookie.
 * All buffers are owned/copied. No sockets, clocks, randomness or callbacks.
 * now is a wrapping 32-bit monotonic millisecond clock, polled regularly.
 */
enum mh_error_code	mh_sctp_open(const struct mh_sctp_config *config,
	struct mh_sctp **output, struct mh_error *error_out);
void			mh_sctp_close(struct mh_sctp *peer);
int			mh_sctp_connected(const struct mh_sctp *peer);
int			mh_sctp_closed(const struct mh_sctp *peer);
enum mh_error_code	mh_sctp_input(struct mh_sctp *peer,
	const unsigned char *data, size_t size, unsigned long now,
	struct mh_error *error_out);

/* Copied bounded send queue; RANGE means full and preserves the queue. */
enum mh_error_code	mh_sctp_send(struct mh_sctp *peer,
	const unsigned char *data, size_t size, struct mh_error *error_out);

/*
 * Drive timers and pop one outgoing SCTP record, or size=0. The caller must
 * retain each returned record until DTLS accepts it; do not poll more output
 * while DTLS is backpressured. Control traffic precedes queued game messages.
 */
enum mh_error_code	mh_sctp_poll(struct mh_sctp *peer, unsigned long now,
	struct mh_sctp_datagram *output, struct mh_error *error_out);

/* Pop one complete message; 1=message, 0=none. Never exposes partial data. */
int	mh_sctp_receive(struct mh_sctp *peer, struct mh_sctp_message *output);

#endif
