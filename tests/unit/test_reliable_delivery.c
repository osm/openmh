#include <stdio.h>
#include <string.h>

#include "backends/network/reliable_delivery.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

struct sender {
	unsigned char	data[MH_RELIABLE_DATAGRAM_BYTES];
	size_t		size;
	unsigned int	calls;
	int		fail;
};

static enum mh_error_code
send_datagram(void *context, const unsigned char *data, size_t size,
	struct mh_error *error_out)
{
	struct sender *sender;

	sender = context;
	CHECK(size > 0 && size <= sizeof(sender->data));

	if (size == 0 || size > sizeof(sender->data))
		return mh_error_set(error_out, MH_ERROR_RANGE, "bad test send");

	memcpy(sender->data, data, size);
	sender->size = size;
	sender->calls++;

	return mh_error_set(error_out,
		sender->fail ? MH_ERROR_IO : MH_ERROR_NONE,
		sender->fail ? "simulated packet send failure" : NULL);
}

static struct mh_reliable_delivery *
create_delivery(struct sender *sender)
{
	struct mh_reliable_delivery *delivery;

	memset(sender, 0, sizeof(*sender));
	delivery = mh_reliable_delivery_create(send_datagram, sender);
	CHECK(delivery != NULL);

	return delivery;
}

static void
acknowledge(struct mh_reliable_delivery *delivery, unsigned char sequence)
{
	struct mh_reliable_payload payload;

	CHECK(mh_reliable_delivery_receive(
		delivery, &sequence, 1, &payload, NULL) == MH_ERROR_NONE);
	CHECK(payload.data == NULL && payload.size == 0);
}

static void
ordering(void)
{
	static const unsigned char first[] = {
		7, 0, 12, 0, 1, 0, 0, 0, 0, 'a', 'b', 0};

	static const unsigned char second[] = {10, 0, 8, 0, 1, 0, 0, 0};
	struct mh_reliable_delivery *delivery;
	struct sender sender;

	delivery = create_delivery(&sender);

	if (delivery == NULL)
		return;

	CHECK(mh_reliable_delivery_send(delivery, first, sizeof(first), 1, 100,
		NULL) == MH_ERROR_NONE);
	CHECK(sender.calls == 1 && sender.data[0] == 2 &&
		sender.size == sizeof(first) + 1);
	CHECK(memcmp(sender.data + 1, first, sizeof(first)) == 0);
	CHECK(mh_reliable_delivery_send(delivery, second, sizeof(second), 1,
		101, NULL) == MH_ERROR_NONE);
	CHECK(sender.calls == 1);
	CHECK(mh_reliable_delivery_tick(delivery, 599, NULL) == MH_ERROR_NONE);
	CHECK(mh_reliable_delivery_tick(delivery, 600, NULL) == MH_ERROR_NONE);
	CHECK(sender.calls == 1);
	CHECK(mh_reliable_delivery_tick(delivery, 601, NULL) == MH_ERROR_NONE);
	CHECK(sender.calls == 2 && sender.data[0] == 2);
	acknowledge(delivery, 3);
	CHECK(mh_reliable_delivery_tick(delivery, 602, NULL) == MH_ERROR_NONE);
	CHECK(sender.calls == 2);
	acknowledge(delivery, 2);
	CHECK(sender.calls == 2 && mh_reliable_delivery_pending(delivery));
	CHECK(mh_reliable_delivery_tick(delivery, 603, NULL) == MH_ERROR_NONE);
	CHECK(sender.calls == 3 && sender.data[0] == 3);
	CHECK(sender.size == sizeof(second) + 1 &&
		memcmp(sender.data + 1, second, sizeof(second)) == 0);
	acknowledge(delivery, 2);
	CHECK(mh_reliable_delivery_pending(delivery));
	acknowledge(delivery, 3);
	CHECK(!mh_reliable_delivery_pending(delivery));
	mh_reliable_delivery_destroy(delivery);
}

static void
lost_ack(void)
{
	static const unsigned char text[] = "chat";
	struct mh_reliable_delivery *host, *client;
	struct mh_reliable_payload payload;
	struct sender host_io, client_io;
	unsigned int i;

	host = create_delivery(&host_io);
	client = create_delivery(&client_io);

	if (host == NULL || client == NULL)
		goto done;

	client_io.fail = 1;
	CHECK(mh_reliable_delivery_send(
		host, text, sizeof(text), 1, 100, NULL) == MH_ERROR_NONE);
	CHECK(mh_reliable_delivery_receive(client, host_io.data, host_io.size,
		&payload, NULL) == MH_ERROR_NONE);
	CHECK(payload.size == sizeof(text) && payload.reliable);
	CHECK(memcmp(payload.data, text, sizeof(text)) == 0);
	CHECK(client_io.calls == 1 && client_io.size == 1 &&
		client_io.data[0] == 2);
	CHECK(mh_reliable_delivery_pending(host));
	CHECK(mh_reliable_delivery_tick(host, 601, NULL) == MH_ERROR_NONE);
	client_io.fail = 0;
	CHECK(mh_reliable_delivery_receive(client, host_io.data, host_io.size,
		&payload, NULL) == MH_ERROR_NONE);
	CHECK(payload.data == NULL && payload.size == 0 &&
		client_io.calls == 2);
	CHECK(mh_reliable_delivery_receive(host, client_io.data, client_io.size,
		&payload, NULL) == MH_ERROR_NONE);
	CHECK(!mh_reliable_delivery_pending(host));

	for (i = 0; i < 2; i++) {
		CHECK(mh_reliable_delivery_send(host, text, sizeof(text), 0,
			700, NULL) == MH_ERROR_NONE);
		CHECK(host_io.data[0] == 0);
		CHECK(mh_reliable_delivery_receive(client, host_io.data,
			host_io.size, &payload, NULL) == MH_ERROR_NONE);
		CHECK(payload.size == sizeof(text) && !payload.reliable);
	}

	CHECK(client_io.calls == 2);

	/* Only the last reliable ID is compared, not numeric sequence order. */
	host_io.data[0] = 3;
	CHECK(mh_reliable_delivery_receive(client, host_io.data, host_io.size,
		&payload, NULL) == MH_ERROR_NONE);
	CHECK(payload.size == sizeof(text));
	host_io.data[0] = 2;
	CHECK(mh_reliable_delivery_receive(client, host_io.data, host_io.size,
		&payload, NULL) == MH_ERROR_NONE);
	CHECK(payload.size == sizeof(text) && client_io.calls == 4);

done:
	mh_reliable_delivery_destroy(host);
	mh_reliable_delivery_destroy(client);
}

static void
send_failures(void)
{
	static const unsigned char data[] = {1, 2, 3};
	struct mh_reliable_delivery *delivery;
	struct sender sender;
	struct mh_error error;

	delivery = create_delivery(&sender);

	if (delivery == NULL)
		return;

	sender.fail = 1;
	CHECK(mh_reliable_delivery_send(delivery, data, sizeof(data), 1, 700,
		&error) == MH_ERROR_NONE);
	CHECK(error.code == MH_ERROR_NONE && error.message == NULL);
	CHECK(mh_reliable_delivery_pending(delivery) && sender.calls == 1);
	CHECK(mh_reliable_delivery_tick(delivery, 1200, NULL) == MH_ERROR_NONE);
	CHECK(sender.calls == 1);
	CHECK(mh_reliable_delivery_tick(delivery, 1201, NULL) == MH_ERROR_IO);
	CHECK(sender.calls == 2 && sender.data[0] == 2);
	CHECK(mh_reliable_delivery_send(delivery, data, sizeof(data), 0, 1800,
		NULL) == MH_ERROR_IO);
	CHECK(sender.calls == 3 && sender.data[0] == 0);
	CHECK(mh_reliable_delivery_flush_head(delivery, 1801, NULL) ==
		MH_ERROR_IO);
	CHECK(mh_reliable_delivery_pending(delivery));
	sender.fail = 0;
	CHECK(mh_reliable_delivery_flush_head(delivery, 1802, NULL) ==
		MH_ERROR_NONE);
	CHECK(!mh_reliable_delivery_pending(delivery) && sender.data[0] == 2);
	CHECK(mh_reliable_delivery_flush_head(delivery, 1803, NULL) ==
		MH_ERROR_NONE);
	CHECK(sender.calls == 5);
	mh_reliable_delivery_destroy(delivery);
}

static void
sequence_wrap(void)
{
	static const unsigned char data[] = {42};
	struct mh_reliable_delivery *delivery;
	struct sender sender;
	unsigned int i, sequence;

	delivery = create_delivery(&sender);

	if (delivery == NULL)
		return;

	for (i = 0; i < 512; i++) {
		sequence = (i + 1) % 255 + 1;
		CHECK(mh_reliable_delivery_send(delivery, data, sizeof(data), 1,
			100 + i, NULL) == MH_ERROR_NONE);
		CHECK(sender.data[0] == sequence && sender.calls == i + 1);
		acknowledge(delivery, (unsigned char)sequence);
		CHECK(!mh_reliable_delivery_pending(delivery));
	}

	/*
	 * Zero is the original unsent timestamp sentinel, including at time
	 * zero.
	 */
	CHECK(mh_reliable_delivery_send(delivery, data, sizeof(data), 1, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_reliable_delivery_tick(delivery, 0, NULL) == MH_ERROR_NONE);
	CHECK(sender.calls == 514);
	mh_reliable_delivery_destroy(delivery);
}

static void
capacity(void)
{
	unsigned char data[MH_RELIABLE_DATAGRAM_BYTES];
	struct mh_reliable_delivery *delivery;
	struct mh_reliable_payload payload;
	struct sender sender;
	unsigned int i;

	memset(data, 0x42, sizeof(data));
	delivery = create_delivery(&sender);

	if (delivery == NULL)
		return;

	/* 12*(8191+9)+(1591+9) fills the original 100000-byte queue exactly. */
	for (i = 0; i < 12; i++)
		CHECK(mh_reliable_delivery_send(delivery, data, 8191, 1, 100,
			NULL) == MH_ERROR_NONE);

	CHECK(mh_reliable_delivery_send(delivery, data, 1591, 1, 100, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_reliable_delivery_send(delivery, data, 1, 1, 100, NULL) ==
		MH_ERROR_BACKPRESSURE);
	CHECK(sender.calls == 1 && sender.size == 8192);
	acknowledge(delivery, 2);
	CHECK(mh_reliable_delivery_send(delivery, data, 8191, 1, 100, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 13; i++) {
		CHECK(mh_reliable_delivery_flush_head(delivery, 101, NULL) ==
			MH_ERROR_NONE);
		CHECK(sender.data[0] == i + 3);
	}

	CHECK(!mh_reliable_delivery_pending(delivery));
	CHECK(mh_reliable_delivery_send(delivery, data, sizeof(data), 1, 100,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_reliable_delivery_send(delivery, data, 0, 1, 100, NULL) ==
		MH_ERROR_INVALID);
	payload.data = data;
	payload.size = 123;
	CHECK(mh_reliable_delivery_receive(delivery, data, 0, &payload, NULL) ==
		MH_ERROR_INVALID);
	CHECK(payload.data == data && payload.size == 123);

	/*
	 * Maximum-sized reliable receive is ACKed and borrowed without
	 * truncation.
	 */
	CHECK(mh_reliable_delivery_receive(delivery, data, sizeof(data),
		&payload, NULL) == MH_ERROR_NONE);
	CHECK(payload.size == sizeof(data) - 1 && payload.data == data + 1);
	CHECK(sender.size == 1 && sender.data[0] == 0x42);
	mh_reliable_delivery_destroy(delivery);
}

int
main(void)
{
	ordering();
	lost_ack();
	send_failures();
	sequence_wrap();
	capacity();

	return failures != 0;
}
