#ifndef MH_BACKENDS_RELIABLE_DELIVERY_H
#define MH_BACKENDS_RELIABLE_DELIVERY_H

#include "engine/core/error.h"

#define MH_RELIABLE_DATAGRAM_BYTES 8192

struct mh_reliable_delivery;

/* Borrowed payload view; ACKs and duplicates return NULL data and zero size. */
struct mh_reliable_payload {
	const unsigned char	*data;
	size_t			size;
	int			reliable;
};

/*
 * Shared by native UDP and WebRTC transports. One admitted peer, independent
 * of sockets and application message framing.
 * The callback sends one complete datagram to that peer. It must consume the
 * borrowed bytes before returning and must not reenter this delivery object.
 * Context is borrowed until destroy. NULL on invalid callback/allocation
 * failure.
 */
struct mh_reliable_delivery	*mh_reliable_delivery_create(
	enum mh_error_code (*send)( void *, const unsigned char *, size_t, struct mh_error *),
	void *context);
void				 mh_reliable_delivery_destroy(
	struct mh_reliable_delivery *delivery);

/*
 * Reliable success means queued, not acknowledged; a failed initial send stays
 * queued. Unreliable send failures are returned directly. Empty payloads are
 * invalid. Time is an unsigned 32-bit millisecond value supplied by the owner.
 */
enum mh_error_code	mh_reliable_delivery_send(
	struct mh_reliable_delivery *delivery, const unsigned char *data,
	size_t size, int reliable, unsigned long now_ms,
	struct mh_error *error_out);

/*
 * Pump only the head, immediately if unsent or strictly after its 500 ms retry.
 */
enum mh_error_code	mh_reliable_delivery_tick(
	struct mh_reliable_delivery *delivery, unsigned long now_ms,
	struct mh_error *error_out);

/*
 * Call only after checking the datagram's source endpoint. Tick before polling
 * for datagrams, as in the original. ACKs retire only a matching queue head;
 * reliable data is ACKed even when duplicated. ACK send failure does not undo
 * delivery. Output borrows datagram bytes until the caller reuses that buffer.
 * Invalid input leaves output unchanged. No caller-sized receive buffer is
 * used here, avoiding the original ACK-before-copy-capacity loss behavior.
 */
enum mh_error_code	mh_reliable_delivery_receive(
	struct mh_reliable_delivery *delivery, const unsigned char *data,
	size_t size, struct mh_reliable_payload *output,
	struct mh_error *error_out);

/*
 * Shutdown only: send/remove one head without waiting for ACK. Failure retains
 * it. Ordinary transport flushing uses tick instead. Destroy frees any queue
 * remaining after the owner's bounded shutdown attempt.
 */
enum mh_error_code	mh_reliable_delivery_flush_head(
	struct mh_reliable_delivery *delivery, unsigned long now_ms,
	struct mh_error *error_out);
int			mh_reliable_delivery_pending(
	const struct mh_reliable_delivery *delivery);

#endif
