#include <limits.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <stdlib.h>
#include <string.h>

#include "backends/network/webrtc_dtls.h"

enum {
	QUEUE_PACKETS = 4,
	DATAGRAM_MTU = 1200
};

struct datagram_queue {
	struct mh_dtls_packet	packets[QUEUE_PACKETS];
	unsigned int		head, count;
	int			peek;
};

struct mh_dtls_identity {
	SSL_CTX		*context;
	BIO_METHOD	*method;
	unsigned char	fingerprint[32];
};

struct mh_dtls_peer {
	SSL			*ssl;
	struct datagram_queue	incoming, outgoing;
	unsigned char		fingerprint[32], pending[MH_DTLS_SEND_BYTES];
	size_t			pending_size;
	int			verified, failed;
};

static int
queue_push(struct datagram_queue *queue, const unsigned char *data, size_t size)
{
	struct mh_dtls_packet *packet;

	if (queue->count == QUEUE_PACKETS || size > MH_DTLS_PACKET_BYTES)
		return 0;

	packet = &queue->packets[(queue->head + queue->count) % QUEUE_PACKETS];
	memcpy(packet->data, data, size);
	packet->size = size;
	queue->count++;

	return 1;
}

static int
bio_create(BIO *bio)
{
	BIO_set_init(bio, 1);
	BIO_set_data(bio, NULL);

	return 1;
}

static int
bio_destroy(BIO *bio)
{
	if (bio != NULL)
		BIO_set_data(bio, NULL);

	return 1;
}

static int
bio_read(BIO *bio, char *output, int capacity)
{
	struct datagram_queue *queue;
	struct mh_dtls_packet *packet;
	size_t size;

	BIO_clear_retry_flags(bio);
	queue = BIO_get_data(bio);

	if (output == NULL || capacity <= 0 || queue == NULL)
		return 0;

	if (queue->count == 0) {
		BIO_set_retry_read(bio);
		return -1;
	}

	packet = &queue->packets[queue->head];
	size = packet->size < (size_t)capacity ? packet->size
					: (size_t)capacity;
	memcpy(output, packet->data, size);

	if (!queue->peek) {
		queue->head = (queue->head + 1) % QUEUE_PACKETS;
		queue->count--;
	}

	return (int)size;
}

static int
bio_write(BIO *bio, const char *data, int size)
{
	struct datagram_queue *queue;

	BIO_clear_retry_flags(bio);
	queue = BIO_get_data(bio);

	if (data == NULL || size <= 0 || size > MH_DTLS_PACKET_BYTES ||
		queue == NULL)
		return -1;

	if (!queue_push(queue, (const unsigned char *)data, (size_t)size)) {
		BIO_set_retry_write(bio);
		return -1;
	}

	return size;
}

static long
bio_control(BIO *bio, int command, long number, void *pointer)
{
	struct datagram_queue *queue;

	(void)pointer;
	queue = BIO_get_data(bio);

	switch (command) {
	case BIO_CTRL_FLUSH:
	case BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT:
		return 1;
	case BIO_CTRL_DGRAM_QUERY_MTU:
	case BIO_CTRL_DGRAM_GET_MTU:
	case BIO_CTRL_DGRAM_GET_FALLBACK_MTU:
		return DATAGRAM_MTU;
	case BIO_CTRL_DGRAM_SET_MTU:
		return number;
	case BIO_CTRL_DGRAM_GET_MTU_OVERHEAD:
		return 48;
	case BIO_CTRL_DGRAM_SET_PEEK_MODE:
		if (queue != NULL)
			queue->peek = number != 0;

		return 1;

	case BIO_CTRL_PENDING:
		return queue != NULL && queue->count != 0
			? (long)queue->packets[queue->head].size
			: 0;
	case BIO_CTRL_EOF:
		return queue == NULL || queue->count == 0;
	default:
		return 0;
	}
}

static int
verify_certificate(int preverified, X509_STORE_CTX *context)
{
	SSL *ssl;
	struct mh_dtls_peer *peer;
	X509 *certificate;
	unsigned char fingerprint[32];
	unsigned int size;

	(void)preverified;
	ssl = X509_STORE_CTX_get_ex_data(
		context, SSL_get_ex_data_X509_STORE_CTX_idx());
	peer = ssl == NULL ? NULL : SSL_get_app_data(ssl);
	certificate = X509_STORE_CTX_get_current_cert(context);

	if (peer == NULL || certificate == NULL ||
		X509_STORE_CTX_get_error_depth(context) != 0 ||
		!X509_digest(certificate, EVP_sha256(), fingerprint, &size) ||
		size != 32 ||
		CRYPTO_memcmp(fingerprint, peer->fingerprint,
			sizeof(peer->fingerprint)) != 0)
		return 0;

	peer->verified = 1;

	return 1;
}

enum mh_error_code
mh_webrtc_random(unsigned char *output, size_t size, struct mh_error *error_out)
{
	if (output == NULL || size == 0 || size > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebRTC random output");

	if (RAND_bytes(output, (int)size) != 1)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"WebRTC random generation failed");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_dtls_identity_close(struct mh_dtls_identity *identity)
{
	if (identity == NULL)
		return;

	SSL_CTX_free(identity->context);
	BIO_meth_free(identity->method);
	free(identity);
}

const unsigned char *
mh_dtls_fingerprint(const struct mh_dtls_identity *identity)
{
	return identity == NULL ? NULL : identity->fingerprint;
}

enum mh_error_code
mh_dtls_identity_open(
	struct mh_dtls_identity **output, struct mh_error *error_out)
{
	struct mh_dtls_identity *identity;
	EVP_PKEY *key;
	X509 *certificate;
	X509_NAME *name;
	unsigned int size;
	int index, ready;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing DTLS identity output");

	identity = calloc(1, sizeof(*identity));

	if (identity == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate DTLS identity");

	key = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "prime256v1");
	certificate = X509_new();
	name = X509_NAME_new();
	identity->context = SSL_CTX_new(DTLS_method());
	index = BIO_get_new_index();
	identity->method = index < 0
		? NULL
		: BIO_meth_new(
			index | BIO_TYPE_SOURCE_SINK, "OpenMH datagrams");
	ready = key != NULL && certificate != NULL && name != NULL &&
		identity->context != NULL && identity->method != NULL;

	if (ready) {
		ready = X509_set_version(certificate, 2) &&
			ASN1_INTEGER_set(
				X509_get_serialNumber(certificate), 1) &&
			X509_gmtime_adj(X509_getm_notBefore(certificate),
				-3600) != NULL &&
			X509_gmtime_adj(X509_getm_notAfter(certificate),
				31536000L) != NULL &&
			X509_set_pubkey(certificate, key) &&
			X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
				(const unsigned char *)"OpenMH", -1, -1, 0) &&
			X509_set_subject_name(certificate, name) &&
			X509_set_issuer_name(certificate, name) &&
			X509_sign(certificate, key, EVP_sha256()) > 0 &&
			X509_digest(certificate, EVP_sha256(),
				identity->fingerprint, &size) &&
			size == 32 &&
			SSL_CTX_use_certificate(
				identity->context, certificate) &&
			SSL_CTX_use_PrivateKey(identity->context, key) &&
			SSL_CTX_check_private_key(identity->context) &&
			SSL_CTX_set_min_proto_version(
				identity->context, DTLS1_2_VERSION) &&
			SSL_CTX_set_max_proto_version(
				identity->context, DTLS1_2_VERSION) &&
			SSL_CTX_set_cipher_list(identity->context,
				"ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES256-GCM-SHA384") &&
			BIO_meth_set_create(identity->method, bio_create) &&
			BIO_meth_set_destroy(identity->method, bio_destroy) &&
			BIO_meth_set_read(identity->method, bio_read) &&
			BIO_meth_set_write(identity->method, bio_write) &&
			BIO_meth_set_ctrl(identity->method, bio_control);
	}

	X509_NAME_free(name);
	EVP_PKEY_free(key);
	X509_free(certificate);

	if (!ready) {
		mh_dtls_identity_close(identity);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot initialize DTLS identity");
	}

	SSL_CTX_set_verify(identity->context,
		SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
		verify_certificate);
	SSL_CTX_set_verify_depth(identity->context, 0);
	SSL_CTX_set_read_ahead(identity->context, 1);
	SSL_CTX_set_max_cert_list(identity->context, 16384);
	SSL_CTX_set_session_cache_mode(identity->context, SSL_SESS_CACHE_OFF);
	SSL_CTX_set_options(identity->context,
		SSL_OP_NO_QUERY_MTU | SSL_OP_NO_RENEGOTIATION |
			SSL_OP_NO_TICKET);
	*output = identity;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_dtls_peer_close(struct mh_dtls_peer *peer)
{
	if (peer == NULL)
		return;

	SSL_free(peer->ssl);
	free(peer);
}

enum mh_error_code
mh_dtls_peer_open(struct mh_dtls_identity *identity, int server,
	const unsigned char fingerprint[32], struct mh_dtls_peer **output,
	struct mh_error *error_out)
{
	struct mh_dtls_peer *peer;
	BIO *incoming, *outgoing;

	if (identity == NULL || fingerprint == NULL || output == NULL ||
		(server != 0 && server != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid DTLS peer configuration");

	peer = calloc(1, sizeof(*peer));

	if (peer == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate DTLS peer");

	peer->ssl = SSL_new(identity->context);
	incoming = BIO_new(identity->method);
	outgoing = BIO_new(identity->method);

	if (peer->ssl == NULL || incoming == NULL || outgoing == NULL) {
		BIO_free(incoming);
		BIO_free(outgoing);
		mh_dtls_peer_close(peer);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate DTLS buffers");
	}

	memcpy(peer->fingerprint, fingerprint, sizeof(peer->fingerprint));
	BIO_set_data(incoming, &peer->incoming);
	BIO_set_data(outgoing, &peer->outgoing);
	SSL_set_bio(peer->ssl, incoming, outgoing);

	if (!SSL_set_app_data(peer->ssl, peer) ||
		SSL_set_mtu(peer->ssl, DATAGRAM_MTU) <= 0) {
		mh_dtls_peer_close(peer);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot configure DTLS peer");
	}

	if (server)
		SSL_set_accept_state(peer->ssl);
	else
		SSL_set_connect_state(peer->ssl);
	*output = peer;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_dtls_connected(const struct mh_dtls_peer *peer)
{
	return peer != NULL && !peer->failed && peer->verified &&
		SSL_is_init_finished(peer->ssl);
}

enum mh_error_code
mh_dtls_input(struct mh_dtls_peer *peer, const unsigned char *data, size_t size,
	struct mh_error *error_out)
{
	if (peer == NULL || data == NULL || size == 0 ||
		size > MH_DTLS_PACKET_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid DTLS datagram");

	if (peer->failed)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "DTLS peer is closed");

	if (!queue_push(&peer->incoming, data, size))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"DTLS receive queue is full");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
ssl_result(struct mh_dtls_peer *peer, int result, struct mh_error *error_out)
{
	int reason;

	reason = SSL_get_error(peer->ssl, result);

	if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	peer->failed = 1;

	return mh_error_set(error_out, MH_ERROR_BACKEND,
		"DTLS connection failed or peer certificate did not match signaling");
}

static enum mh_error_code
write_pending(struct mh_dtls_peer *peer, struct mh_error *error_out)
{
	int result;

	if (peer->pending_size == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	ERR_clear_error();
	result = SSL_write(peer->ssl, peer->pending, (int)peer->pending_size);

	if (result <= 0)
		return ssl_result(peer, result, error_out);

	if ((size_t)result != peer->pending_size) {
		peer->failed = 1;
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"partial DTLS record write");
	}

	peer->pending_size = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_dtls_send(struct mh_dtls_peer *peer, const unsigned char *data, size_t size,
	struct mh_error *error_out)
{
	if (data == NULL || size == 0 || size > MH_DTLS_SEND_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid DTLS plaintext size");

	if (!mh_dtls_connected(peer))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"DTLS peer is not connected");

	if (peer->pending_size != 0)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "DTLS send queue is full");

	memcpy(peer->pending, data, size);
	peer->pending_size = size;

	return write_pending(peer, error_out);
}

enum mh_error_code
mh_dtls_poll(struct mh_dtls_peer *peer, struct mh_dtls_packet *output,
	struct mh_error *error_out)
{
	struct mh_dtls_packet packet;
	int result;
	enum mh_error_code status;

	if (peer == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid DTLS poll");

	if (peer->failed)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "DTLS peer is closed");

	packet.size = 0;
	ERR_clear_error();

	if (DTLSv1_handle_timeout(peer->ssl) < 0) {
		peer->failed = 1;
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, "DTLS retry failed");
	}

	if (!SSL_is_init_finished(peer->ssl)) {
		ERR_clear_error();
		result = SSL_do_handshake(peer->ssl);

		if (result <= 0) {
			status = ssl_result(peer, result, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		} else if (!peer->verified) {
			peer->failed = 1;
			return mh_error_set(error_out, MH_ERROR_PERMISSION,
				"DTLS certificate was not verified");
		}
	}

	if (mh_dtls_connected(peer)) {
		status = write_pending(peer, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		/* Retry the same SSL write before starting a read operation. */
		if (peer->pending_size != 0) {
			output->size = 0;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		ERR_clear_error();
		result = SSL_read(peer->ssl, packet.data, sizeof(packet.data));

		if (result > 0)
			packet.size = (size_t)result;
		else {
			status = ssl_result(peer, result, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	/* Unused bytes are not part of the returned record. */
	output->size = packet.size;

	if (packet.size != 0)
		memcpy(output->data, packet.data, packet.size);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_dtls_outgoing(struct mh_dtls_peer *peer, struct mh_dtls_packet *output)
{
	struct datagram_queue *queue;
	struct mh_dtls_packet *packet;

	if (peer == NULL || output == NULL || peer->outgoing.count == 0)
		return 0;

	queue = &peer->outgoing;
	packet = &queue->packets[queue->head];
	output->size = packet->size;
	memcpy(output->data, packet->data, packet->size);
	queue->head = (queue->head + 1) % QUEUE_PACKETS;
	queue->count--;

	return 1;
}
