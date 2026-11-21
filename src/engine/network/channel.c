#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "engine/network/channel.h"

/* Versioned independently of legacy application/transport framing. */
#define CHANNEL_MAGIC "OMQ1"
#define SEQUENCE_OFFSET 4
#define ACK_OFFSET 8
#define ACK_DELAY_OFFSET 12
#define SEQUENCE_HALF 0x80000000UL

static unsigned long
next_sequence(unsigned long sequence)
{
	sequence = (sequence + 1) & MH_U32_MAX;

	return sequence == 0 ? 1 : sequence;
}

void
mh_channel_header(const struct mh_channel *channel, double now_ms,
	unsigned char header[MH_CHANNEL_HEADER_BYTES])
{
	double delay;

	delay = channel->received_sequence == 0 ? 0
						: now_ms - channel->received_ms;

	if (delay < 0)
		delay = 0;

	if (delay > MH_CHANNEL_DELAY_LIMIT_MS)
		delay = MH_CHANNEL_DELAY_LIMIT_MS;

	memcpy(header, CHANNEL_MAGIC, SEQUENCE_OFFSET);
	mh_write_u32le(header + SEQUENCE_OFFSET,
		next_sequence(channel->sent_sequence));
	mh_write_u32le(header + ACK_OFFSET, channel->received_sequence);
	mh_write_u32le(header + ACK_DELAY_OFFSET, (unsigned long)delay);
}

void
mh_channel_sent(struct mh_channel *channel, double now_ms, int reliable)
{
	struct mh_channel_sent *sent;

	channel->sent_sequence = next_sequence(channel->sent_sequence);
	channel->sent_ms = now_ms;
	sent = &channel->sent[channel->sent_sequence % MH_CHANNEL_HISTORY];
	sent->sequence = reliable ? 0 : channel->sent_sequence;
	sent->time_ms = now_ms;
}

static void
acknowledge(struct mh_channel *channel, unsigned long ack, unsigned long delay,
	double now_ms)
{
	struct mh_channel_sent *sent;
	double elapsed;

	if (ack == 0)
		return;

	sent = &channel->sent[ack % MH_CHANNEL_HISTORY];

	if (sent->sequence != ack)
		return;

	elapsed = now_ms - sent->time_ms;
	sent->sequence = 0;

	if (elapsed < 0 || elapsed > MH_CHANNEL_SAMPLE_LIMIT_MS)
		return;

	/* Remove reported ACK scheduling time, never subtract below zero. */
	elapsed = elapsed > delay ? elapsed - delay : 0;

	if (channel->sample_count == MH_CHANNEL_HISTORY)
		channel->sum_ms -= channel->samples[channel->next_sample];
	else
		channel->sample_count++;

	channel->samples[channel->next_sample] = elapsed;
	channel->next_sample = (channel->next_sample + 1) % MH_CHANNEL_HISTORY;
	channel->sum_ms += elapsed;
	channel->latest_ms = elapsed;
}

enum mh_error_code
mh_channel_receive(struct mh_channel *channel, const unsigned char *packet,
	size_t size, int reliable, double now_ms, struct mh_error *error_out)
{
	unsigned long sequence, ack, delay, distance;

	if (channel == NULL || packet == NULL ||
		size < MH_CHANNEL_HEADER_BYTES ||
		memcmp(packet, CHANNEL_MAGIC, SEQUENCE_OFFSET) != 0 ||
		!(now_ms >= 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid OpenMH channel header");

	sequence = mh_read_u32le(packet + SEQUENCE_OFFSET);
	ack = mh_read_u32le(packet + ACK_OFFSET);
	delay = mh_read_u32le(packet + ACK_DELAY_OFFSET);

	if (sequence == 0 || delay > MH_CHANNEL_DELAY_LIMIT_MS ||
		(ack == 0 && delay != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid OpenMH acknowledgement");

	distance = (sequence - channel->received_sequence) & MH_U32_MAX;

	if (channel->received_sequence == 0 ||
		(distance != 0 && distance < SEQUENCE_HALF)) {
		channel->received_sequence = sequence;
		channel->received_ms = now_ms;
	}

	if (!reliable)
		acknowledge(channel, ack, delay, now_ms);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
