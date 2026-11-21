#include <stdlib.h>
#include <string.h>

#include "backends/network/webrtc_sctp_packet.h"
#include "backends/network/webrtc_sdp.h"
#include "backends/network/webrtc_stun.h"
#include "backends/network/webrtc_wire.h"

int	LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

static void
check_sdp(const unsigned char *data, size_t size)
{
	struct mh_rtc_offer offer, before;
	struct mh_rtc_answer answer;
	char output[2048];
	size_t written;

	memset(&offer, 0, sizeof(offer));
	memcpy(&before, &offer, sizeof(before));

	if (mh_rtc_offer_read(data, size, &offer, NULL) != MH_ERROR_NONE) {
		if (memcmp(&offer, &before, sizeof(offer)) != 0)
			abort();

		return;
	}

	memset(&answer, 0, sizeof(answer));
	strcpy(answer.ufrag, "server");
	strcpy(answer.password, "01234567890123456789012345678901");
	answer.address_size = 4;
	answer.address[0] = 127;
	answer.address[3] = 1;
	answer.udp_port = answer.sctp_port = 5000;

	if (mh_rtc_answer_write(&offer, &answer, output, sizeof(output),
		&written, NULL) != MH_ERROR_NONE ||
		written != strlen(output))
		abort();
}

static void
check_stun(const unsigned char *data, size_t size)
{
	struct mh_stun_message stun, stun_before;
	unsigned char address[16];
	unsigned int port;
	size_t count;

	memset(&stun, 0, sizeof(stun));
	memcpy(&stun_before, &stun, sizeof(stun));

	if (mh_stun_read(data, size, &stun, NULL) == MH_ERROR_NONE) {
		if (stun.mapped != NULL)
			mh_stun_mapped(&stun, address, &count, &port, NULL);
	} else if (memcmp(&stun, &stun_before, sizeof(stun)) != 0)
		abort();
}

static void
check_sctp(const unsigned char *data, size_t size)
{
	struct mh_sctp_packet sctp, sctp_before;
	struct mh_sctp_chunk chunk;
	unsigned char encoded[MH_SCTP_PACKET_BYTES];
	size_t offset, previous, written;

	memset(&sctp, 0, sizeof(sctp));
	memcpy(&sctp_before, &sctp, sizeof(sctp));

	if (mh_sctp_packet_read(data, size, &sctp, NULL) != MH_ERROR_NONE) {
		if (memcmp(&sctp, &sctp_before, sizeof(sctp)) != 0)
			abort();

		return;
	}

	if (mh_sctp_packet_begin(sctp.source, sctp.destination, sctp.tag,
		encoded, sizeof(encoded), &written, NULL) != MH_ERROR_NONE)
		abort();

	offset = MH_SCTP_HEADER_BYTES;
	previous = offset;

	while (mh_sctp_chunk_next(&sctp, &offset, &chunk) == 1) {
		if (offset <= previous || offset > size)
			abort();

		previous = offset;

		if (mh_sctp_chunk_append(encoded, sizeof(encoded), &written,
			chunk.type, chunk.flags, chunk.data, chunk.size,
			NULL) != MH_ERROR_NONE)
			abort();
	}

	if (mh_sctp_packet_finish(encoded, written, NULL) != MH_ERROR_NONE ||
		mh_sctp_packet_read(encoded, written, &sctp, NULL) !=
			MH_ERROR_NONE)
		abort();
}

int
LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	unsigned char repaired[MH_SCTP_PACKET_BYTES];
	unsigned long crc;

	check_stun(data, size);
	check_sctp(data, size);
	check_sdp(data, size);

	/*
	 * Exercise attributes/chunks behind the checksum as well as raw
	 * failures.
	 */
	if (size <= sizeof(repaired)) {
		memcpy(repaired, data, size);

		if (mh_sctp_packet_finish(repaired, size, NULL) ==
			MH_ERROR_NONE)
			check_sctp(repaired, size);

		if (size >= 28 && size <= MH_STUN_PACKET_BYTES &&
			mh_rtc_read16(data + size - 8) == 0x8028 &&
			mh_rtc_read16(data + size - 6) == 4) {
			memcpy(repaired, data, size);
			crc = mh_rtc_crc(data, size - 8, (size_t)-1,
				0xedb88320UL) ^
				0x5354554eUL;
			mh_rtc_write32(repaired + size - 4, crc);
			check_stun(repaired, size);
		}
	}

	return 0;
}
