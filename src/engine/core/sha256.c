#include <string.h>

#include "engine/core/limits.h"
#include "engine/core/sha256.h"

/* FIPS 180-4, sections 4.2.2, 5.3.3 and 6.2.2. */
static const unsigned long round_constants[] = {0x428a2f98UL, 0x71374491UL,
	0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL,
	0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
	0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL, 0xe49b69c1UL,
	0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL, 0x2de92c6fUL, 0x4a7484aaUL,
	0x5cb0a9dcUL, 0x76f988daUL, 0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL,
	0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
	0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 0x650a7354UL,
	0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL, 0xa2bfe8a1UL, 0xa81a664bUL,
	0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL, 0xd6990624UL, 0xf40e3585UL,
	0x106aa070UL, 0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
	0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL, 0x748f82eeUL,
	0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL, 0x90befffaUL, 0xa4506cebUL,
	0xbef9a3f7UL, 0xc67178f2UL};

static unsigned long
rotate_right(unsigned long word, unsigned int count)
{
	return ((word >> count) | (word << (32 - count))) & MH_U32_MAX;
}

static unsigned long
read_word(const unsigned char *bytes)
{
	return ((unsigned long)bytes[0] << 24) |
		((unsigned long)bytes[1] << 16) |
		((unsigned long)bytes[2] << 8) | bytes[3];
}

static void
write_word(unsigned char *bytes, unsigned long word)
{
	bytes[0] = (unsigned char)(word >> 24);
	bytes[1] = (unsigned char)(word >> 16);
	bytes[2] = (unsigned char)(word >> 8);
	bytes[3] = (unsigned char)word;
}

static void
compress_block(struct mh_sha256 *hash)
{
	unsigned long schedule[64];
	unsigned long a, b, c, d, e, f, g, h;
	unsigned long first, second, lower, upper, choose, majority;
	unsigned int i;

	for (i = 0; i < 16; i++)
		schedule[i] = read_word(hash->block + i * 4);

	for (i = 16; i < 64; i++) {
		lower = rotate_right(schedule[i - 15], 7) ^
			rotate_right(schedule[i - 15], 18) ^
			(schedule[i - 15] >> 3);
		upper = rotate_right(schedule[i - 2], 17) ^
			rotate_right(schedule[i - 2], 19) ^
			(schedule[i - 2] >> 10);
		schedule[i] =
			(schedule[i - 16] + lower + schedule[i - 7] + upper) &
			MH_U32_MAX;
	}

	a = hash->words[0];
	b = hash->words[1];
	c = hash->words[2];
	d = hash->words[3];
	e = hash->words[4];
	f = hash->words[5];
	g = hash->words[6];
	h = hash->words[7];

	for (i = 0; i < 64; i++) {
		upper = rotate_right(e, 6) ^ rotate_right(e, 11) ^
			rotate_right(e, 25);
		choose = (e & f) ^ (~e & g);
		first = (h + upper + choose + round_constants[i] +
				schedule[i]) &
			MH_U32_MAX;
		lower = rotate_right(a, 2) ^ rotate_right(a, 13) ^
			rotate_right(a, 22);
		majority = (a & b) ^ (a & c) ^ (b & c);
		second = (lower + majority) & MH_U32_MAX;
		h = g;
		g = f;
		f = e;
		e = (d + first) & MH_U32_MAX;
		d = c;
		c = b;
		b = a;
		a = (first + second) & MH_U32_MAX;
	}

	hash->words[0] = (hash->words[0] + a) & MH_U32_MAX;
	hash->words[1] = (hash->words[1] + b) & MH_U32_MAX;
	hash->words[2] = (hash->words[2] + c) & MH_U32_MAX;
	hash->words[3] = (hash->words[3] + d) & MH_U32_MAX;
	hash->words[4] = (hash->words[4] + e) & MH_U32_MAX;
	hash->words[5] = (hash->words[5] + f) & MH_U32_MAX;
	hash->words[6] = (hash->words[6] + g) & MH_U32_MAX;
	hash->words[7] = (hash->words[7] + h) & MH_U32_MAX;
}

void
mh_sha256_init(struct mh_sha256 *hash)
{
	static const unsigned long initial[] = {0x6a09e667UL, 0xbb67ae85UL,
		0x3c6ef372UL, 0xa54ff53aUL, 0x510e527fUL, 0x9b05688cUL,
		0x1f83d9abUL, 0x5be0cd19UL};

	memset(hash, 0, sizeof(*hash));
	memcpy(hash->words, initial, sizeof(initial));
}

void
mh_sha256_update(struct mh_sha256 *hash, const void *data, size_t size)
{
	const unsigned char *bytes;
	unsigned long previous;
	size_t count;

	bytes = data;

	while (size > 0) {
		count = MH_SHA256_BLOCK_BYTES - hash->used;

		if (count > size)
			count = size;

		memcpy(hash->block + hash->used, bytes, count);
		hash->used += count;
		previous = hash->bits_low;
		hash->bits_low =
			(previous + (unsigned long)count * 8) & MH_U32_MAX;

		if (hash->bits_low < previous)
			hash->bits_high = (hash->bits_high + 1) & MH_U32_MAX;

		if (hash->used == MH_SHA256_BLOCK_BYTES) {
			compress_block(hash);
			hash->used = 0;
		}

		bytes += count;
		size -= count;
	}
}

void
mh_sha256_finish(struct mh_sha256 *hash, unsigned char digest[MH_SHA256_BYTES])
{
	unsigned char padding[MH_SHA256_BLOCK_BYTES];
	unsigned char length[8];
	size_t count;
	unsigned int i;

	write_word(length, hash->bits_high);
	write_word(length + 4, hash->bits_low);
	memset(padding, 0, sizeof(padding));
	padding[0] = 0x80;
	count = hash->used < 56 ? 56 - hash->used : 120 - hash->used;
	mh_sha256_update(hash, padding, count);
	mh_sha256_update(hash, length, sizeof(length));

	for (i = 0; i < 8; i++)
		write_word(digest + i * 4, hash->words[i]);
}

void
mh_sha256_text(const unsigned char digest[MH_SHA256_BYTES],
	char text[MH_SHA256_TEXT_SIZE])
{
	static const char digits[] = "0123456789abcdef";
	unsigned int i;

	for (i = 0; i < MH_SHA256_BYTES; i++) {
		text[i * 2] = digits[digest[i] >> 4];
		text[i * 2 + 1] = digits[digest[i] & 15];
	}

	text[MH_SHA256_TEXT_SIZE - 1] = '\0';
}

void
mh_sha256_hmac(const void *key, size_t key_size, const void *data, size_t size,
	unsigned char digest[MH_SHA256_BYTES])
{
	enum {
		INNER_PAD = 0x36,
		OUTER_PAD = 0x5c
	};
	struct mh_sha256 hash;
	unsigned char block[MH_SHA256_BLOCK_BYTES];
	size_t i;

	memset(block, 0, sizeof(block));

	if (key_size > sizeof(block)) {
		mh_sha256_init(&hash);
		mh_sha256_update(&hash, key, key_size);
		mh_sha256_finish(&hash, block);
	} else if (key_size != 0)
		memcpy(block, key, key_size);

	for (i = 0; i < sizeof(block); i++)
		block[i] ^= INNER_PAD;

	mh_sha256_init(&hash);
	mh_sha256_update(&hash, block, sizeof(block));
	mh_sha256_update(&hash, data, size);
	mh_sha256_finish(&hash, digest);

	for (i = 0; i < sizeof(block); i++)
		block[i] ^= INNER_PAD ^ OUTER_PAD;

	mh_sha256_init(&hash);
	mh_sha256_update(&hash, block, sizeof(block));
	mh_sha256_update(&hash, digest, MH_SHA256_BYTES);
	mh_sha256_finish(&hash, digest);
}

int
mh_digest_equal(const unsigned char *a, const unsigned char *b, size_t size)
{
	unsigned int diff;
	size_t i;

	diff = 0;

	for (i = 0; i < size; i++)
		diff |= a[i] ^ b[i];

	return diff == 0;
}
