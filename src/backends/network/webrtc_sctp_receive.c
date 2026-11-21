#include <string.h>

#include "backends/network/webrtc_sctp_internal.h"

unsigned long
mh_sctp_distance(unsigned long newer, unsigned long older)
{
	return (newer - older) & SCTP_MASK;
}

int
mh_sctp_after(unsigned long a, unsigned long b)
{
	unsigned long distance;

	distance = mh_sctp_distance(a, b);

	return distance != 0 && distance < SCTP_SERIAL_HALF;
}

static struct sctp_fragment *
find_fragment(struct mh_sctp *peer, unsigned long tsn)
{
	unsigned int i;

	for (i = 0; i < SCTP_FRAGMENTS; i++)
		if (peer->fragments[i].size != 0 &&
			peer->fragments[i].tsn == tsn)
			return &peer->fragments[i];

	return NULL;
}

static void
queue_message(struct mh_sctp *peer, const unsigned char *data, size_t size)
{
	struct mh_sctp_message *message;

	/* Unreliable delivery permits dropping complete messages under load. */
	if (peer->message_count == SCTP_QUEUE)
		return;

	message = &peer->messages[(peer->message_head + peer->message_count) %
		SCTP_QUEUE];
	memcpy(message->data, data, size);
	message->size = size;
	peer->message_count++;
}

static int
assemble(struct mh_sctp *peer)
{
	struct mh_sctp_message message;
	struct sctp_fragment *fragment, *first;
	unsigned int i, count, j;
	unsigned long tsn;

	for (i = 0; i < SCTP_FRAGMENTS; i++) {
		first = &peer->fragments[i];

		if (first->size == 0 || !(first->flags & SCTP_DATA_BEGIN))
			continue;

		tsn = first->tsn;
		message.size = 0;

		for (count = 0; count < SCTP_FRAGMENTS; count++) {
			fragment = find_fragment(peer, tsn);

			if (fragment == NULL)
				break;

			if ((count != 0 &&
				(fragment->flags & SCTP_DATA_BEGIN)) ||
				fragment->size >
					sizeof(message.data) - message.size)
				return 0;

			memcpy(message.data + message.size, fragment->data,
				fragment->size);
			message.size += fragment->size;

			if (fragment->flags & SCTP_DATA_END) {
				queue_message(peer, message.data, message.size);
				tsn = first->tsn;

				for (j = 0; j <= count; j++) {
					find_fragment(peer, tsn)->size = 0;
					tsn = (tsn + 1) & SCTP_MASK;
				}

				break;
			}

			tsn = (tsn + 1) & SCTP_MASK;
		}
	}

	return 1;
}

static void
advance_received(struct mh_sctp *peer)
{
	unsigned long next;

	for (;;) {
		next = (peer->receive_tsn + 1) & SCTP_MASK;

		if (!peer->received[next % SCTP_WINDOW])
			break;

		peer->received[next % SCTP_WINDOW] = 0;
		peer->receive_tsn = next;
	}
}

int
mh_sctp_data_input(struct mh_sctp *peer, const struct mh_sctp_chunk *chunk)
{
	struct sctp_fragment *fragment;
	unsigned long tsn, distance;
	unsigned int i;
	size_t size;

	if (chunk->size <= SCTP_DATA_HEADER_BYTES ||
		chunk->size - SCTP_DATA_HEADER_BYTES > MH_SCTP_MESSAGE_BYTES ||
		!(chunk->flags & SCTP_DATA_UNORDERED) ||
		(chunk->flags &
			~(unsigned int)(SCTP_DATA_BEGIN | SCTP_DATA_END |
				SCTP_DATA_UNORDERED | SCTP_DATA_IMMEDIATE)) !=
			0 ||
		mh_rtc_read16(chunk->data + SCTP_DATA_STREAM) != SCTP_CHANNEL ||
		mh_rtc_read32(chunk->data + SCTP_DATA_PPID) != SCTP_BINARY_PPID)
		return 0;

	tsn = mh_rtc_read32(chunk->data);
	distance = mh_sctp_distance(tsn, peer->receive_tsn);
	peer->sack = 1;

	if (distance == 0 || distance > SCTP_WINDOW ||
		peer->received[tsn % SCTP_WINDOW])
		return 1;

	size = chunk->size - SCTP_DATA_HEADER_BYTES;

	if ((chunk->flags & (SCTP_DATA_BEGIN | SCTP_DATA_END)) ==
		(SCTP_DATA_BEGIN | SCTP_DATA_END))
		queue_message(peer, chunk->data + SCTP_DATA_HEADER_BYTES, size);
	else {
		fragment = NULL;

		for (i = 0; i < SCTP_FRAGMENTS; i++)
			if (peer->fragments[i].size == 0) {
				fragment = &peer->fragments[i];
				break;
			}

		/*
		 * Do not acknowledge a fragment for which no storage is
		 * available.
		 */
		if (fragment == NULL)
			return 1;

		fragment->tsn = tsn;
		fragment->flags = chunk->flags;
		fragment->size = size;
		memcpy(fragment->data, chunk->data + SCTP_DATA_HEADER_BYTES,
			size);

		if (!assemble(peer))
			return 0;
	}

	peer->received[tsn % SCTP_WINDOW] = 1;
	advance_received(peer);

	return 1;
}

int
mh_sctp_forward_input(struct mh_sctp *peer, const struct mh_sctp_chunk *chunk)
{
	unsigned long tsn, distance, old;
	unsigned long cursor;
	unsigned int i, j;
	struct sctp_fragment *fragment;
	int remove[SCTP_FRAGMENTS];

	/*
	 * This association has only unordered data: no ordered stream entries.
	 */
	if (chunk->flags != 0 || chunk->size != SCTP_TSN_BYTES)
		return 0;

	tsn = mh_rtc_read32(chunk->data);
	peer->sack = 1;

	if (!mh_sctp_after(tsn, peer->receive_tsn))
		return 1;

	old = peer->receive_tsn;
	distance = mh_sctp_distance(tsn, old);

	if (distance >= SCTP_WINDOW)
		memset(peer->received, 0, sizeof(peer->received));
	else
		for (i = 1; i <= distance; i++)
			peer->received[((old + i) & SCTP_MASK) % SCTP_WINDOW] =
				0;

	/*
	 * Preserve an intact prefix whose missing tail is beyond FORWARD TSN.
	 * Discard a fragment only when its message crosses a skipped TSN.
	 */
	memset(remove, 0, sizeof(remove));

	for (i = 0; i < SCTP_FRAGMENTS; i++) {
		if (peer->fragments[i].size == 0)
			continue;

		cursor = peer->fragments[i].tsn;

		for (j = 0; j < SCTP_FRAGMENTS; j++) {
			fragment = find_fragment(peer, cursor);

			if (fragment == NULL) {
				remove[i] = !mh_sctp_after(cursor, tsn);
				break;
			}

			if (fragment->flags & SCTP_DATA_BEGIN)
				break;

			cursor = (cursor - 1) & SCTP_MASK;
		}

		cursor = peer->fragments[i].tsn;

		for (j = 0; j < SCTP_FRAGMENTS && !remove[i]; j++) {
			fragment = find_fragment(peer, cursor);

			if (fragment == NULL) {
				remove[i] = !mh_sctp_after(cursor, tsn);
				break;
			}

			if (fragment->flags & SCTP_DATA_END)
				break;

			cursor = (cursor + 1) & SCTP_MASK;
		}
	}

	for (i = 0; i < SCTP_FRAGMENTS; i++)
		if (remove[i])
			peer->fragments[i].size = 0;

	peer->receive_tsn = tsn;
	advance_received(peer);

	return 1;
}

size_t
mh_sctp_sack_write(const struct mh_sctp *peer, unsigned char *data)
{
	unsigned int i, first, gaps;
	unsigned long buffered;

	buffered = 0;

	for (i = 0; i < SCTP_FRAGMENTS; i++)
		buffered += (unsigned long)peer->fragments[i].size;

	for (i = 0; i < peer->message_count; i++)
		buffered += (unsigned long)peer
				->messages[(peer->message_head + i) %
					SCTP_QUEUE]
				.size;

	mh_rtc_write32(data, peer->receive_tsn);
	mh_rtc_write32(data + SCTP_SACK_WINDOW,
		buffered < SCTP_RECEIVE_BYTES ? SCTP_RECEIVE_BYTES - buffered
					: 0);
	gaps = 0;

	for (i = 1; i <= SCTP_WINDOW; i++) {
		if (!peer->received[((peer->receive_tsn + i) & SCTP_MASK) %
			SCTP_WINDOW])
			continue;

		first = i;

		while (i < SCTP_WINDOW &&
			peer->received[((peer->receive_tsn + i + 1) &
					SCTP_MASK) %
				SCTP_WINDOW])
			i++;

		mh_rtc_write16(
			data + SCTP_SACK_HEADER_BYTES + gaps * SCTP_GAP_BYTES,
			first);
		mh_rtc_write16(data + SCTP_SACK_HEADER_BYTES + SCTP_GAP_END +
				gaps * SCTP_GAP_BYTES,
			i);
		gaps++;
	}

	mh_rtc_write16(data + SCTP_SACK_GAPS, gaps);
	mh_rtc_write16(data + SCTP_SACK_DUPLICATES, 0);

	return SCTP_SACK_HEADER_BYTES + gaps * SCTP_GAP_BYTES;
}

int
mh_sctp_receive(struct mh_sctp *peer, struct mh_sctp_message *output)
{
	struct mh_sctp_message *message;

	if (peer == NULL || output == NULL || peer->message_count == 0 ||
		peer->state != SCTP_ESTABLISHED)
		return 0;

	message = &peer->messages[peer->message_head];
	output->size = message->size;
	memcpy(output->data, message->data, message->size);
	peer->message_head = (peer->message_head + 1) % SCTP_QUEUE;
	peer->message_count--;

	return 1;
}
