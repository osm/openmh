#ifndef MH_NET_CHANNEL_H
#define MH_NET_CHANNEL_H

#include "engine/core/error.h"

#define MH_CHANNEL_HEADER_BYTES 16
#define MH_CHANNEL_HISTORY 64
#define MH_CHANNEL_IDLE_MS 100
#define MH_CHANNEL_DELAY_LIMIT_MS 1000
#define MH_CHANNEL_SAMPLE_LIMIT_MS 10000

struct mh_channel_sent {
	unsigned long	sequence;
	double		time_ms;
};

/* Per connection; zero-init/reset on admission, never on race transitions. */
struct mh_channel {
	unsigned long		sent_sequence, received_sequence;
	double			sent_ms, received_ms;
	struct mh_channel_sent	sent[MH_CHANNEL_HISTORY];
	double			samples[MH_CHANNEL_HISTORY], sum_ms, latest_ms;
	unsigned int		sample_count, next_sample;
};

/*
 * Prepare does not commit a send. Only record after the transport accepts it.
 * Reliable retransmissions cannot provide unambiguous RTT samples.
 */
void	mh_channel_header(const struct mh_channel *channel, double now_ms,
	unsigned char header[MH_CHANNEL_HEADER_BYTES]);
void	mh_channel_sent(struct mh_channel *channel, double now_ms,
	int reliable);

/*
 * Parse before changing state; duplicate ACKs never become new samples.
 * Reliable ACK delivery never contributes an RTT sample.
 * Payload order/reliability belongs to the transport, not this envelope.
 */
enum mh_error_code	mh_channel_receive(struct mh_channel *channel,
	const unsigned char *packet, size_t size, int reliable, double now_ms,
	struct mh_error *error_out);

#endif
