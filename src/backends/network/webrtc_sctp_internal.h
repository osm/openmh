#ifndef MH_WEBRTC_SCTP_INTERNAL_H
#define MH_WEBRTC_SCTP_INTERNAL_H

#include "backends/network/webrtc_sctp.h"
#include "backends/network/webrtc_sctp_packet.h"
#include "backends/network/webrtc_wire.h"

#define SCTP_MASK 0xffffffffUL
#define SCTP_SERIAL_HALF 0x80000000UL
#define SCTP_WINDOW 64
#define SCTP_FRAGMENTS 16
#define SCTP_QUEUE 4
#define SCTP_PAYLOAD 1024
#define SCTP_DATA_HEADER_BYTES 12
#define SCTP_INIT_HEADER_BYTES 16
#define SCTP_SACK_HEADER_BYTES 12
#define SCTP_PARAMETER_HEADER_BYTES 4
#define SCTP_GAP_BYTES 4
#define SCTP_TSN_BYTES 4
#define SCTP_CHUNK_BYTES                                                       \
	(SCTP_PAYLOAD + MH_SCTP_CHUNK_HEADER_BYTES + SCTP_DATA_HEADER_BYTES)
#define SCTP_CONTROL_BYTES                                                     \
	(MH_SCTP_DATAGRAM_BYTES - MH_SCTP_HEADER_BYTES -                       \
		MH_SCTP_CHUNK_HEADER_BYTES)
#define SCTP_RECEIVE_BYTES 65536UL
#define SCTP_MIN_REMOTE_WINDOW 1500
#define SCTP_RTO_INITIAL_MS 3000
#define SCTP_RTO_MIN_MS 1000
#define SCTP_RTO_MAX_MS 60000
#define SCTP_FAST_MISSES 3
#define SCTP_INITIAL_CHUNKS 4

/* Offsets within chunk payloads, after the common chunk header. */
enum {
	SCTP_INIT_WINDOW = 4,
	SCTP_INIT_OUT_STREAMS = 8,
	SCTP_INIT_IN_STREAMS = 10,
	SCTP_INIT_TSN = 12,
	SCTP_DATA_STREAM = 4,
	SCTP_DATA_SEQUENCE = 6,
	SCTP_DATA_PPID = 8,
	SCTP_SACK_WINDOW = 4,
	SCTP_SACK_GAPS = 8,
	SCTP_SACK_DUPLICATES = 10,
	SCTP_GAP_END = 2,
	SCTP_PARAMETER_LENGTH = 2
};

enum sctp_chunk_type {
	SCTP_DATA = 0,
	SCTP_INIT = 1,
	SCTP_INIT_ACK = 2,
	SCTP_SACK = 3,
	SCTP_HEARTBEAT = 4,
	SCTP_HEARTBEAT_ACK = 5,
	SCTP_ABORT = 6,
	SCTP_SHUTDOWN_REQUEST = 7,
	SCTP_SHUTDOWN_ACK = 8,
	SCTP_ERROR = 9,
	SCTP_COOKIE_ECHO = 10,
	SCTP_COOKIE_ACK = 11,
	SCTP_SHUTDOWN_COMPLETE = 14,
	SCTP_PADDING = 132,
	SCTP_FORWARD_TSN = 192
};

enum sctp_data_flag {
	SCTP_DATA_END = 1,
	SCTP_DATA_BEGIN = 2,
	SCTP_DATA_UNORDERED = 4,
	SCTP_DATA_IMMEDIATE = 8
};

enum sctp_parameter_type {
	SCTP_HEARTBEAT_INFO = 1,
	SCTP_IPV4_ADDRESS = 5,
	SCTP_IPV6_ADDRESS = 6,
	SCTP_STATE_COOKIE = 7,
	SCTP_COOKIE_PRESERVATIVE = 9,
	SCTP_SUPPORTED_ADDRESSES = 12,
	SCTP_FORWARD_SUPPORTED = 0xc000
};

enum {
	SCTP_BINARY_PPID = 53,
	SCTP_CHANNEL = 0,
	SCTP_CAUSE_UNKNOWN_CHUNK = 6,
	SCTP_UNKNOWN_CHUNK_SKIP = 0x80,
	SCTP_UNKNOWN_CHUNK_REPORT = 0x40,
	SCTP_UNKNOWN_PARAMETER_SKIP = 0x8000
};

enum sctp_state {
	SCTP_LISTEN,
	SCTP_COOKIE,
	SCTP_ESTABLISHED,
	SCTP_SHUTDOWN,
	SCTP_CLOSED
};

struct sctp_fragment {
	unsigned long	tsn;
	unsigned int	flags;
	size_t		size;
	unsigned char	data[MH_SCTP_MESSAGE_BYTES];
};

struct sctp_flight {
	unsigned long	tsn, message, sent;
	size_t		size;
	unsigned int	misses;
	int		acked, abandoned;
};

struct sctp_queued {
	struct mh_sctp_message	message;
	size_t			offset;
	unsigned long		first_tsn;
};

struct mh_sctp {
	struct mh_sctp_config	config;
	enum sctp_state		state;
	unsigned long		remote_tag, remote_initial, receive_tsn;
	unsigned char		received[SCTP_WINDOW];
	struct sctp_fragment	fragments[SCTP_FRAGMENTS];
	struct mh_sctp_message	messages[SCTP_QUEUE];
	unsigned int		message_head, message_count;
	int			sack, init_ack, cookie_ack, shutdown_ack;
	unsigned char		heartbeat[SCTP_CONTROL_BYTES];
	size_t			heartbeat_size;
	unsigned char		report[SCTP_CONTROL_BYTES];
	size_t			report_size;
	struct sctp_queued	queued[SCTP_QUEUE];
	unsigned int		queue_head, queue_count;
	struct sctp_flight	flight[SCTP_WINDOW];
	unsigned int		flight_head, flight_count;
	unsigned long		next_tsn, send_ack, remote_window;
	unsigned long		cwnd, threshold, partial_acked;
	unsigned long		srtt, variance, rto, timer_started, last_send;
	unsigned long		recovery_end, forward_tsn, forward_sent;
	int	timer_running, recovering, forward_pending, forward_due;
};

/* Serial arithmetic is explicitly 32-bit even on LP64 hosts. */
unsigned long	mh_sctp_distance(unsigned long newer, unsigned long older);
int		mh_sctp_after(unsigned long a, unsigned long b);
int		mh_sctp_data_input(struct mh_sctp *peer,
	const struct mh_sctp_chunk *chunk);
int		mh_sctp_forward_input(struct mh_sctp *peer,
	const struct mh_sctp_chunk *chunk);
size_t		mh_sctp_sack_write(const struct mh_sctp *peer,
	unsigned char *data);
int		mh_sctp_sack_input(struct mh_sctp *peer,
	const struct mh_sctp_chunk *chunk, unsigned long now);
void		mh_sctp_send_tick(struct mh_sctp *peer, unsigned long now);
void		mh_sctp_send_shutdown(struct mh_sctp *peer);
int		mh_sctp_data_write(struct mh_sctp *peer, unsigned long now,
	unsigned char *data, size_t *size, unsigned int *flags);

#endif
