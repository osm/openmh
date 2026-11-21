#include <stdlib.h>
#include <string.h>

#include "backends/network/reliable_delivery.h"
#include "engine/core/limits.h"

/* European 3.1 accounts nine bytes per queued application payload. */
#define QUEUE_BYTES 100000UL
#define RECORD_BYTES 9
#define RETRY_MS 500UL
#define SEQUENCE_MAX 255

struct queued_packet {
	struct queued_packet	*next;
	size_t			size;
	unsigned long		sent_ms;
};

struct mh_reliable_delivery {
	enum mh_error_code (*send)(
		void	*, const unsigned char *, size_t, struct mh_error *);
	void			*context;
	struct queued_packet	*head, *tail;
	unsigned long		queued_bytes;
	unsigned int		sent_sequence, received_sequence;
};

/* Datagram bytes follow the private native bookkeeping, with no struct cast. */
static unsigned char *
packet_data(struct queued_packet *packet)
{
	return (unsigned char *)(packet + 1);
}

struct mh_reliable_delivery *
mh_reliable_delivery_create(
	enum mh_error_code (*send)(
		void *, const unsigned char *, size_t, struct mh_error *),
	void *context)
{
	struct mh_reliable_delivery *delivery;

	if (send == NULL)
		return NULL;

	delivery = calloc(1, sizeof(*delivery));

	if (delivery == NULL)
		return NULL;

	delivery->send = send;
	delivery->context = context;
	delivery->sent_sequence = 1;

	return delivery;
}

static void
remove_head(struct mh_reliable_delivery *delivery)
{
	struct queued_packet *packet;

	packet = delivery->head;
	delivery->head = packet->next;
	delivery->queued_bytes -= packet->size - 1 + RECORD_BYTES;

	if (delivery->head == NULL)
		delivery->tail = NULL;

	free(packet);
}

void
mh_reliable_delivery_destroy(struct mh_reliable_delivery *delivery)
{
	if (delivery == NULL)
		return;

	while (delivery->head != NULL)
		remove_head(delivery);

	free(delivery);
}

static enum mh_error_code
enqueue(struct mh_reliable_delivery *delivery, const unsigned char *data,
	size_t size, struct mh_error *error_out)
{
	struct queued_packet *packet;
	unsigned char *bytes;

	if (size + RECORD_BYTES > QUEUE_BYTES - delivery->queued_bytes)
		return mh_error_set(error_out, MH_ERROR_BACKPRESSURE,
			"reliable queue is full");

	packet = malloc(sizeof(*packet) + size + 1);

	if (packet == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate reliable packet");

	packet->next = NULL;
	packet->size = size + 1;
	packet->sent_ms = 0;
	delivery->sent_sequence = delivery->sent_sequence % SEQUENCE_MAX + 1;
	bytes = packet_data(packet);
	bytes[0] = (unsigned char)delivery->sent_sequence;
	memcpy(bytes + 1, data, size);

	if (delivery->tail != NULL)
		delivery->tail->next = packet;
	else
		delivery->head = packet;

	delivery->tail = packet;
	delivery->queued_bytes += size + RECORD_BYTES;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_head(struct mh_reliable_delivery *delivery, unsigned long now_ms,
	struct mh_error *error_out)
{
	struct queued_packet *packet;

	packet = delivery->head;

	/* Failed sends still start the retry timer. */
	packet->sent_ms = now_ms;

	return delivery->send(delivery->context, packet_data(packet),
		packet->size, error_out);
}

enum mh_error_code
mh_reliable_delivery_tick(struct mh_reliable_delivery *delivery,
	unsigned long now_ms, struct mh_error *error_out)
{
	struct queued_packet *packet;
	unsigned long deadline;

	if (delivery == NULL || now_ms > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid delivery clock");

	packet = delivery->head;

	if (packet == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/* Preserve the original u32 addition and strict deadline comparison. */
	deadline = (packet->sent_ms + RETRY_MS) & MH_U32_MAX;

	if (packet->sent_ms != 0 && now_ms <= deadline)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return send_head(delivery, now_ms, error_out);
}

enum mh_error_code
mh_reliable_delivery_send(struct mh_reliable_delivery *delivery,
	const unsigned char *data, size_t size, int reliable,
	unsigned long now_ms, struct mh_error *error_out)
{
	unsigned char datagram[MH_RELIABLE_DATAGRAM_BYTES];
	enum mh_error_code status;

	if (delivery == NULL || data == NULL || size == 0 ||
		size >= sizeof(datagram) || now_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid delivery payload");

	if (reliable) {
		status = enqueue(delivery, data, size, error_out);
	} else {
		datagram[0] = 0;
		memcpy(datagram + 1, data, size);
		status = delivery->send(
			delivery->context, datagram, size + 1, error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	/* Return enqueue/send success even if this pump fails. */
	(void)mh_reliable_delivery_tick(delivery, now_ms, NULL);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_reliable_delivery_receive(struct mh_reliable_delivery *delivery,
	const unsigned char *data, size_t size,
	struct mh_reliable_payload *output, struct mh_error *error_out)
{
	struct mh_reliable_payload payload;
	unsigned int sequence;

	if (delivery == NULL || data == NULL || size == 0 ||
		size > MH_RELIABLE_DATAGRAM_BYTES || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid delivery datagram");

	payload.data = NULL;
	payload.size = 0;
	payload.reliable = 0;
	sequence = data[0];

	if (size == 1) {
		if (delivery->head != NULL &&
			sequence == packet_data(delivery->head)[0])
			remove_head(delivery);
	} else {
		if (sequence != 0)
			(void)delivery->send(delivery->context, data, 1, NULL);

		if (sequence == 0 || sequence != delivery->received_sequence) {
			if (sequence != 0)
				delivery->received_sequence = sequence;

			payload.data = data + 1;
			payload.size = size - 1;
			payload.reliable = sequence != 0;
		}
	}

	*output = payload;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_reliable_delivery_flush_head(struct mh_reliable_delivery *delivery,
	unsigned long now_ms, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (delivery == NULL || now_ms > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid shutdown flush");

	if (delivery->head == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = send_head(delivery, now_ms, error_out);

	if (status == MH_ERROR_NONE)
		remove_head(delivery);

	return status;
}

int
mh_reliable_delivery_pending(const struct mh_reliable_delivery *delivery)
{
	return delivery != NULL && delivery->head != NULL;
}
