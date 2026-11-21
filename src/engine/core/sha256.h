#ifndef MH_CORE_SHA256_H
#define MH_CORE_SHA256_H

#include <stddef.h>

enum {
	MH_SHA256_BYTES = 32,
	MH_SHA256_TEXT_SIZE = MH_SHA256_BYTES * 2 + 1,
	MH_SHA256_BLOCK_BYTES = 64
};

/* Incremental file identity, using 32-bit words on both ILP32 and LP64. */
struct mh_sha256 {
	unsigned long	words[8];
	unsigned long	bits_low, bits_high;
	unsigned char	block[MH_SHA256_BLOCK_BYTES];
	size_t		used;
};

void	mh_sha256_init(struct mh_sha256 *hash);

/* data may be NULL only for an empty span. */
void	mh_sha256_update(struct mh_sha256 *hash, const void *data, size_t size);

/* Finishes the state; initialize it again before hashing another file. */
void	mh_sha256_finish(struct mh_sha256 *hash,
	unsigned char digest[MH_SHA256_BYTES]);
void	mh_sha256_text(const unsigned char digest[MH_SHA256_BYTES],
	char text[MH_SHA256_TEXT_SIZE]);

void	mh_sha256_hmac(const void *key, size_t key_size, const void *data,
	size_t size, unsigned char digest[MH_SHA256_BYTES]);
int	mh_digest_equal(const unsigned char *a, const unsigned char *b,
	size_t size);

#endif
