#ifndef MH_BACKENDS_WEBRTC_SERVER_INTERNAL_H
#define MH_BACKENDS_WEBRTC_SERVER_INTERNAL_H

#include "backends/network/reliable_delivery.h"
#include "backends/network/udp_multiplex.h"
#include "backends/network/webrtc_dtls.h"
#include "backends/network/webrtc_provider.h"
#include "backends/network/webrtc_sctp.h"
#include "backends/network/webrtc_sdp.h"

#define RTC_OFFER_TIMEOUT_MS 15000
#define RTC_ICE_IDLE_MS 30000
#define RTC_POLL_DATAGRAMS 32
#define RTC_PUMP_RECORDS 8
#define RTC_ICE_UFRAG_RANDOM_BYTES 16
#define RTC_ICE_PASSWORD_RANDOM_BYTES 32
#define RTC_WORD_BYTES 4
#define RTC_USERNAME_BYTES                                                     \
	(2 * RTC_ICE_UFRAG_RANDOM_BYTES + 1 + MH_RTC_ICE_BYTES)
#define RTC_STUN_REPLY_BYTES 80
#define RTC_SCTP_PORT 5000
#define RTC_SDP_ANSWER_BYTES 2048
#define RTC_IPV4_FIRST_UNICAST 0x01000000UL
#define RTC_IPV4_FIRST_MULTICAST 0xe0000000UL

struct mh_webrtc_peer {
	unsigned long			id;
	int	nominated, announced, closing, has_client;
	double				opened_ms, ice_ms;
	struct mh_udp_address		remote;
	struct mh_transport_address	client;
	struct mh_rtc_offer		offer;
	struct mh_sctp_config		sctp_config;
	char				username[RTC_USERNAME_BYTES];
	char				password[2 * RTC_ICE_PASSWORD_RANDOM_BYTES + 1];
	const char			*reason;
	struct mh_dtls_peer		*dtls;
	struct mh_sctp			*sctp;
	struct mh_reliable_delivery	*delivery;
	struct mh_sctp_datagram		outgoing;
};

struct mh_webrtc_server {
	struct mh_webrtc_server_config	config;
	struct mh_udp_socket		*socket;
	struct mh_udp_address		local;
	struct mh_dtls_identity		*identity;
	struct mh_webrtc_peer		peers[MH_WEBRTC_SERVER_PEERS];
	unsigned long			last_id, clock_word;
	double				elapsed_ms;
	unsigned int			next_peer;
};

int		mh_webrtc_udp_packet(const unsigned char *data, size_t size);
unsigned int	mh_webrtc_server_port(const struct mh_webrtc_server *server);

void			mh_webrtc_server_time(struct mh_webrtc_server *server);
void			mh_webrtc_peer_clear(struct mh_webrtc_peer *peer);
void			mh_webrtc_peer_finish(struct mh_webrtc_peer *peer,
	const char *reason);
void			mh_webrtc_server_expire(
	struct mh_webrtc_server *server);
enum mh_error_code	mh_webrtc_peer_send(void *context,
	const unsigned char *data, size_t size, struct mh_error *error_out);
enum mh_error_code	mh_webrtc_peer_flush(struct mh_webrtc_server *server,
	struct mh_webrtc_peer *peer, struct mh_error *error_out);
enum mh_error_code	mh_webrtc_peer_pump(struct mh_webrtc_server *server,
	struct mh_webrtc_peer *peer, struct mh_error *error_out);
enum mh_error_code	mh_webrtc_server_read(struct mh_webrtc_server *server,
	struct mh_error *error_out);

#endif
