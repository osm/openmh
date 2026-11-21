#include <stdio.h>
#include <string.h>

#include "engine/core/sha256.h"

static int failures;

struct padding_case {
	size_t		size;
	const char	*digest;
};

static void
check_digest(struct mh_sha256 *hash, const char *expected)
{
	unsigned char digest[MH_SHA256_BYTES];
	char text[MH_SHA256_TEXT_SIZE];

	mh_sha256_finish(hash, digest);
	mh_sha256_text(digest, text);

	if (strcmp(text, expected) == 0)
		return;

	fprintf(stderr, "SHA-256: expected %s, got %s\n", expected, text);
	failures++;
}

static void
check_known_messages(void)
{
	struct mh_sha256 hash;
	unsigned char block[1000];
	unsigned int i;

	mh_sha256_init(&hash);
	mh_sha256_update(&hash, NULL, 0);
	check_digest(&hash,
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	mh_sha256_init(&hash);
	mh_sha256_update(&hash, "abc", 3);
	check_digest(&hash,
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	mh_sha256_init(&hash);
	memset(block, 'a', sizeof(block));

	for (i = 0; i < 1000; i++)
		mh_sha256_update(&hash, block, sizeof(block));

	check_digest(&hash,
		"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void
check_padding(void)
{
	/*
	 * Independent hashlib results for bytes(i % 251 for i in range(size)).
	 */
	static const struct padding_case cases[] = {
		{55, "463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59"},
		{56, "da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562"},
		{63, "29af2686fd53374a36b0846694cc342177e428d1647515f078784d69cdb9e488"},
		{64, "fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108"},
		{65, "4bfd2c8b6f1eec7a2afeb48b934ee4b2694182027e6d0fc075074f2fabb31781"}};

	struct mh_sha256 hash;
	unsigned char bytes[65];
	size_t i, split, test;

	for (i = 0; i < sizeof(bytes); i++)
		bytes[i] = (unsigned char)i;

	for (test = 0; test < sizeof(cases) / sizeof(cases[0]); test++) {
		for (split = 0; split <= cases[test].size; split++) {
			mh_sha256_init(&hash);
			mh_sha256_update(&hash, bytes, split);
			mh_sha256_update(
				&hash, bytes + split, cases[test].size - split);
			check_digest(&hash, cases[test].digest);
		}
	}
}

int
main(void)
{
	unsigned char key[20], digest[MH_SHA256_BYTES];
	char text[MH_SHA256_TEXT_SIZE];

	/* RFC 4231 test case 1. */
	memset(key, 0x0b, sizeof(key));
	mh_sha256_hmac(key, sizeof(key), "Hi There", 8, digest);
	mh_sha256_text(digest, text);

	if (strcmp(text,
		"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"))
		failures++;

	check_known_messages();
	check_padding();

	return failures != 0;
}
