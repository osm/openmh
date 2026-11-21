#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/settings/address_book.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Address book line %d: %s\n",          \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
sorting(void)
{
	static const char *const names[] = {
		"protected", "z", "A", "a", "A", "\200", "A"};

	static const unsigned int expected[] = {0, 2, 4, 6, 3, 1, 5};
	struct mh_address_entry entries[7], original[7];
	struct mh_address_book book;
	unsigned int previous[7], i;

	memset(entries, 0, sizeof(entries));

	for (i = 0; i < 7; i++) {
		strcpy(entries[i].profile.name, names[i]);
		sprintf(entries[i].filename, "%u.NPC", i);
		sprintf(entries[i].profile.ip, "host%u.example", i);
	}

	/* The original compares bytes after the terminator as well. */
	entries[6].profile.name[31] = 1;
	memcpy(original, entries, sizeof(entries));
	book.entries = entries;
	book.count = 7;
	mh_address_book_sort(&book, previous);

	for (i = 0; i < book.count; i++) {
		CHECK(previous[i] == expected[i]);
		CHECK(memcmp(&entries[i], &original[expected[i]],
			sizeof(*entries)) == 0);
	}

	book.count = 1;
	mh_address_book_sort(&book, NULL);
	CHECK(memcmp(entries, original, sizeof(*entries)) == 0);
	book.count = 0;
	book.entries = NULL;
	mh_address_book_sort(&book, NULL);

	return 0;
}

int
main(void)
{
	static const unsigned char text[] =
		"Address ignored\nType TCP\nDescription a   long\tname\nAddress game.com extra\n";
	static const char *const names[] = {
		"alpha.npc", "ALPHA!.NPC", "!.npc", "!!.NPC"};

	struct mh_address_profile profile, before;
	struct mh_address_source sources[3];
	struct mh_address_book book, prior;
	struct mh_address_entry entries[3];
	char output[512], saved[512], long_ip[200], bad_name[32];
	size_t size, queried;
	unsigned int i;

	CHECK(sorting() == 0);
	CHECK(mh_address_profile_read(text, sizeof(text) - 1, 2, &profile,
		NULL) == MH_ERROR_NONE);
	CHECK(profile.tcp && strcmp(profile.name, "a long name") == 0 &&
		strcmp(profile.ip, "game.com") == 0);
	CHECK(mh_address_profile_encode(&profile, output, sizeof(output), &size,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_address_profile_encode(&profile, NULL, 0, &queried, NULL) ==
			MH_ERROR_NONE &&
		queried == size);
	before = profile;
	CHECK(mh_address_profile_read((const unsigned char *)output, size, 2,
		&profile, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(profile.name, before.name) == 0 &&
		strcmp(profile.ip, before.ip) == 0);
	memset(saved, 0xa5, sizeof(saved));
	memcpy(output, saved, sizeof(output));
	queried = 123;
	CHECK(mh_address_profile_encode(&profile, output, 1, &queried, NULL) !=
			MH_ERROR_NONE &&
		queried == 123);
	CHECK(memcmp(output, saved, sizeof(output)) == 0);
	memset(long_ip, 'x', sizeof(long_ip));
	memcpy(long_ip, "Type TCP\nAddress ", 17);
	long_ip[sizeof(long_ip) - 1] = 0;
	CHECK(mh_address_profile_read((const unsigned char *)long_ip,
		strlen(long_ip), 2, &profile, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&profile, &before, sizeof(profile)) == 0);
	CHECK(mh_address_profile_read((const unsigned char *)"Type TCP", 8,
		4096, &profile, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(profile.name, "TCP/IP_4096") == 0 && profile.ip[0] == 0);
	CHECK(mh_address_filename("A:l/p.ha", names, 4, output, NULL) ==
			MH_ERROR_NONE &&
		strcmp(output, "alpha!!.npc") == 0);
	CHECK(mh_address_filename("/;\xff", names, 4, output, NULL) ==
			MH_ERROR_NONE &&
		strcmp(output, "!!!.npc") == 0);
	memset(bad_name, 'x', sizeof(bad_name));
	memcpy(output, saved, sizeof(output));
	CHECK(mh_address_filename(bad_name, names, 4, output, NULL) !=
			MH_ERROR_NONE &&
		memcmp(output, saved, sizeof(output)) == 0);

	memset(&book, 0, sizeof(book));
	CHECK(mh_address_book_read(NULL, 0, &book, NULL) == MH_ERROR_NONE &&
		book.count == 1);
	CHECK(strcmp(book.entries[0].profile.name, "TCP/IP") == 0 &&
		book.entries[0].profile.ip[0] == 0);
	CHECK(mh_address_book_remove(&book, 0, NULL) != MH_ERROR_NONE &&
		book.count == 1);
	CHECK(mh_address_book_add(&book, "Alpha", "game.com", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_address_book_add(&book, "Alpha", "127.0.0.1", NULL) ==
		MH_ERROR_NONE);
	CHECK(book.count == 3 &&
		strcmp(book.entries[2].filename, "alpha!.npc") == 0);
	prior = book;
	memcpy(entries, book.entries, sizeof(entries));
	CHECK(mh_address_book_add(&book, bad_name, "ip", NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&prior, &book, sizeof(book)) == 0 &&
		memcmp(entries, book.entries, sizeof(entries)) == 0);
	CHECK(mh_address_book_remove(&book, 1, NULL) == MH_ERROR_NONE &&
		book.count == 2);
	CHECK(strcmp(book.entries[1].profile.ip, "127.0.0.1") == 0);
	mh_address_book_free(&book);
	mh_address_book_free(&book);

	for (i = 0; i < 3; i++) {
		sources[i].text = text;
		sources[i].size = sizeof(text) - 1;
	}

	sources[0].filename = "alpha.npc";
	sources[1].filename = "beta.NPC";
	sources[2].filename = "gamma.npc";
	sources[0].text = (const unsigned char *)"Type IPX";
	sources[0].size = 8;
	sources[1].text = NULL;
	sources[1].size = 0;
	sources[2].text = (const unsigned char *)"Type TCP";
	sources[2].size = 8;
	CHECK(mh_address_book_read(sources, 3, &book, NULL) == MH_ERROR_NONE &&
		book.count == 2);
	CHECK(strcmp(book.entries[1].profile.name, "TCP/IP_04") == 0 &&
		strcmp(book.entries[1].filename, "gamma.npc") == 0);
	mh_address_book_free(&book);
	sources[2].filename = "ALPHA.NPC";
	CHECK(mh_address_book_read(sources, 3, &book, NULL) != MH_ERROR_NONE &&
		book.entries == NULL && book.count == 0);
	sources[2].filename = "../escape.npc";
	CHECK(mh_address_book_read(sources, 3, &book, NULL) != MH_ERROR_NONE &&
		book.entries == NULL);
	sources[2].filename = "gamma.npc";
	sources[2].text = (const unsigned char *)"\1";
	sources[2].size = 1;
	CHECK(mh_address_book_read(sources, 3, &book, NULL) != MH_ERROR_NONE &&
		book.entries == NULL);

	return 0;
}
