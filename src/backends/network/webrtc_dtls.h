#ifndef MH_BACKENDS_WEBRTC_DTLS_H
#define MH_BACKENDS_WEBRTC_DTLS_H

#include "engine/core/error.h"

#define MH_DTLS_PACKET_BYTES 18432
#define MH_DTLS_SEND_BYTES 1100

struct mh_dtls_identity;
struct mh_dtls_peer;

struct mh_dtls_packet {
	size_t		size;
	unsigned char	data[MH_DTLS_PACKET_BYTES];
};

/* Ephemeral P-256 certificate/key. Peers borrow identity until their close. */
enum mh_error_code	 mh_dtls_identity_open(struct mh_dtls_identity **output,
	struct mh_error *error_out);
void			 mh_dtls_identity_close(
	struct mh_dtls_identity *identity);
const unsigned char	*mh_dtls_fingerprint(
	const struct mh_dtls_identity *identity);
enum mh_error_code	 mh_webrtc_random(unsigned char *output, size_t size,
	struct mh_error *error_out);

/*
 * DTLS 1.2 over bounded datagram queues, without sockets, threads or callbacks
 * into game code. Fingerprint pins the peer certificate from trusted signaling;
 * CA verification is deliberately replaced by this exact SHA-256 comparison.
 * server=1 is passive; server=0 is useful for native interoperability tests.
 * Main thread only. Open failures preserve output. Close cancels all work.
 */
enum mh_error_code	mh_dtls_peer_open(struct mh_dtls_identity *identity,
	int server, const unsigned char fingerprint[32],
	struct mh_dtls_peer **output, struct mh_error *error_out);
void			mh_dtls_peer_close(struct mh_dtls_peer *peer);
int			mh_dtls_connected(const struct mh_dtls_peer *peer);

/* Copy one complete received datagram. A full input queue returns RANGE. */
enum mh_error_code	mh_dtls_input(struct mh_dtls_peer *peer,
	const unsigned char *data, size_t size, struct mh_error *error_out);

/* Advance handshake/retries and copy one plaintext record, or size=0. */
enum mh_error_code	mh_dtls_poll(struct mh_dtls_peer *peer,
	struct mh_dtls_packet *output, struct mh_error *error_out);

/* Copy one message for nonblocking encryption; at most one pending write. */
enum mh_error_code	mh_dtls_send(struct mh_dtls_peer *peer,
	const unsigned char *data, size_t size, struct mh_error *error_out);

/* 1=copied one outgoing UDP datagram, 0=none. Drain after every operation. */
int	mh_dtls_outgoing(struct mh_dtls_peer *peer,
	struct mh_dtls_packet *output);

#endif
