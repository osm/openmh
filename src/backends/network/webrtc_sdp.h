#ifndef MH_WEBRTC_SDP_H
#define MH_WEBRTC_SDP_H

#include "engine/core/error.h"

#define MH_RTC_SDP_BYTES 16384
#define MH_RTC_ICE_BYTES 257
#define MH_RTC_MID_BYTES 64
#define MH_RTC_MESSAGE_BYTES 8192

struct mh_rtc_offer {
	char		ufrag[MH_RTC_ICE_BYTES], password[MH_RTC_ICE_BYTES];
	char		mid[MH_RTC_MID_BYTES];
	unsigned char	fingerprint[32];
	unsigned int	sctp_port;
	unsigned long	maximum_message;
};

struct mh_rtc_answer {
	/*
	 * Cryptographic randomness and certificate generation belong to the
	 * host.
	 */
	char		ufrag[33], password[MH_RTC_ICE_BYTES];
	unsigned char	fingerprint[32];
	unsigned char	address[16];
	size_t		address_size;
	unsigned int	udp_port, sctp_port;
	unsigned long	session_id;
};

/*
 * Read a single browser data-channel offer, with SHA-256 fingerprint and
 * actpass/active DTLS setup. Session defaults may be overridden by media
 * attributes. Reject duplicate interpreted fields, extra media sections,
 * ICE-lite peers and unsupported transports. Candidate addresses are ignored:
 * the ICE-lite server learns the endpoint through authenticated STUN requests.
 * Input is length-delimited; no NUL is needed. CRLF and LF lines are accepted.
 * Errors preserve output. These functions neither allocate nor perform I/O.
 */
enum mh_error_code	mh_rtc_offer_read(const unsigned char *data,
	size_t size, struct mh_rtc_offer *output, struct mh_error *error_out);

/*
 * Produce a passive DTLS/ICE-lite answer with one numeric host candidate.
 * A public/reachable endpoint must be supplied by the host. Output is NUL
 * terminated; written excludes NUL. Errors preserve bytes and written.
 */
enum mh_error_code	mh_rtc_answer_write(const struct mh_rtc_offer *offer,
	const struct mh_rtc_answer *answer, char *output, size_t capacity,
	size_t *written, struct mh_error *error_out);

#endif
