#ifndef MH_WEBRTC_SCTP_PACKET_H
#define MH_WEBRTC_SCTP_PACKET_H

#include "engine/core/error.h"

#define MH_SCTP_PACKET_BYTES 16384
#define MH_SCTP_HEADER_BYTES 12
#define MH_SCTP_CHUNK_HEADER_BYTES 4
#define MH_SCTP_CHUNK_FLAGS 1
#define MH_SCTP_CHUNK_LENGTH 2

struct mh_sctp_packet {
	const unsigned char	*data;
	size_t			size;
	unsigned int		source, destination;
	unsigned long		tag;
};

struct mh_sctp_chunk {
	unsigned int		type, flags;
	const unsigned char	*data;
	size_t			size;
};

/*
 * RFC 9260 framing and CRC32C validation. Borrow unchanged packet bytes;
 * association owners check ports/tags and interpret chunk payloads. All chunks
 * are checked before exposing any. Errors preserve outputs.
 */
enum mh_error_code	mh_sctp_packet_read(const unsigned char *data,
	size_t size, struct mh_sctp_packet *output, struct mh_error *error_out);

/* Start offset at MH_SCTP_HEADER_BYTES. 1=chunk, 0=end, -1=invalid cursor. */
int	mh_sctp_chunk_next(const struct mh_sctp_packet *packet, size_t *offset,
	struct mh_sctp_chunk *output);

/*
 * Construct a packet using begin, append and finish. Size includes its header.
 * Append payload excludes the four-byte chunk header; padding is zeroed.
 * Finish requires at least one chunk and fills the checksum. No overlapping
 * input/output storage. Failure preserves output bytes and size.
 */
enum mh_error_code	mh_sctp_packet_begin(unsigned int source,
	unsigned int destination, unsigned long tag, unsigned char *output,
	size_t capacity, size_t *size, struct mh_error *error_out);
enum mh_error_code	mh_sctp_chunk_append(unsigned char *output,
	size_t capacity, size_t *size, unsigned int type, unsigned int flags,
	const unsigned char *data, size_t length, struct mh_error *error_out);
enum mh_error_code	mh_sctp_packet_finish(unsigned char *output,
	size_t size, struct mh_error *error_out);

#endif
