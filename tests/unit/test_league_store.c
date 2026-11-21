#define _XOPEN_SOURCE 700
#include <sys/resource.h>
#include <sys/stat.h>

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "backends/storage/filesystem_file_read.h"
#include "client/diagnostics/storage_league_file.h"
#include "client/diagnostics/storage_league_store.h"
#include "engine/core/error.h"
#include "engine/storage/league_file.h"
#include "engine/storage/league_store.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "League store line %d: %s\n",          \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)
#define TRY(x)                                                                 \
	do {                                                                   \
		status = (x);                                                  \
		if (status != MH_ERROR_NONE) {                                 \
			fprintf(stderr, "League store line %d: %s\n",          \
				__LINE__, error.message);                      \
			return 1;                                              \
		}                                                              \
	} while (0)

static void
join_path(char output[256], const char *base, const char *name)
{
	size_t base_length, name_length;

	base_length = strlen(base);
	name_length = strlen(name);

	if (base_length + name_length + 2 > 256)
		abort();

	memcpy(output, base, base_length);
	output[base_length] = '/';
	memcpy(output + base_length + 1, name, name_length + 1);
}

static int
write_text(const char *file, const char *text)
{
	FILE *stream;
	int okay;

	stream = fopen(file, "wb");

	if (stream == NULL)
		return 0;

	okay = fputs(text, stream) >= 0;

	if (fclose(stream) != 0)
		okay = 0;

	return okay;
}

static int
file_matches(const char *path_name, const unsigned char *data, size_t size)
{
	struct mh_file file;
	int equal;

	if (mh_file_read(path_name, &file, NULL) != MH_ERROR_NONE)
		return 0;

	equal = file.size == size && memcmp(file.data, data, size) == 0;
	free(file.data);

	return equal;
}

static unsigned int
directory_entries(const char *directory)
{
	DIR *stream;
	struct dirent *item;
	unsigned int count;

	stream = opendir(directory);

	if (stream == NULL)
		abort();

	count = 0;

	while ((item = readdir(stream)) != NULL)
		if (strcmp(item->d_name, ".") != 0 &&
			strcmp(item->d_name, "..") != 0)
			count++;

	if (closedir(stream) != 0)
		abort();

	return count;
}

int
main(void)
{
	struct mh_error error;
	enum mh_error_code status;
	char *temporary;
	char assets[256], user[256], moved[256], alias[256], named[256],
		other[256], held[256], ignored[256];
	struct mh_league_store *store, *fresh;
	struct mh_league_catalog catalog, empty;
	struct mh_league_document document, loaded, invalid;
	struct mh_file saved_file, original_file;
	struct stat info;
	struct rlimit limit, small;
	void (*handler)(int);
	unsigned int i, j;

	CHECK((temporary = mh_test_directory("mh-league-store-")) != NULL);
	join_path(assets, temporary, "assets");
	join_path(user, temporary, "Leagues");
	join_path(moved, temporary, "moved");
	join_path(alias, temporary, "alias");
	join_path(named, user, "Player.lGf");
	join_path(other, user, "other.LGF");
	join_path(held, user, "held");
	join_path(ignored, user, "ignore.txt");
	CHECK(mkdir(assets, 0700) == 0 && mkdir(user, 0700) == 0 &&
		write_text(ignored, "leave me"));
	memset(&document, 0, sizeof(document));
	strcpy(document.name, "Sj\366n");
	document.created = 20261014;

	for (i = 0; i < 26; i++) {
		document.league.entries[i].driver = i;
		sprintf(document.profiles[i].name, "Driver%u", i);
		sprintf(document.profiles[i].nick, "Nick%u", i);
		strcpy(document.profiles[i].horn, "Honker1");
		document.profiles[i].league_fields = 31;

		for (j = 0; j < 4; j++)
			strcpy(document.profiles[i].cars[j], "ASC II");
	}

	document.league.entries[18].human = 1;
	memset(&catalog, 0, sizeof(catalog));
	memset(&empty, 0, sizeof(empty));
	store = fresh = NULL;
	TRY(mh_league_store_open(
		assets, user, document.profiles, 26, &catalog, &store, &error));
	CHECK(catalog.count == 0 && directory_entries(user) == 1);
	TRY(mh_league_store_save(store, "Player.lGf", &document, &error));
	CHECK(catalog.count == 0 &&
		directory_entries(user) ==
			2); /* No implicit live-state mutation. */
	TRY(mh_league_file_load(named, document.profiles, 26, &loaded, &error));
	CHECK(strcmp(loaded.name, "Sj\366n") == 0 &&
		loaded.created == 20261014);
	CHECK(stat(named, &info) == 0 && (info.st_mode & 0777) == 0600 &&
		info.st_nlink == 1);
	document.league.race_index = 7;
	TRY(mh_league_store_save(store, "Player.lGf", &document, &error));
	TRY(mh_league_file_load(named, document.profiles, 26, &loaded, &error));
	CHECK(loaded.league.race_index == 7 && directory_entries(user) == 2);
	TRY(mh_file_read(named, &saved_file, &error));
	invalid = document;
	invalid.profiles[0].horn[0] = 0;
	CHECK(mh_league_store_save(store, "Player.lGf", &invalid, NULL) !=
			MH_ERROR_NONE &&
		file_matches(named, saved_file.data, saved_file.size));
	CHECK(mh_league_store_save(store, "player.lgf", &document, NULL) !=
			MH_ERROR_NONE &&
		file_matches(named, saved_file.data, saved_file.size));
	CHECK(mh_league_store_save(store, "../bad.LGF", &document, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_league_store_remove(store, "missing.LGF", NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_league_store_remove(store, "player.lgf", NULL) !=
			MH_ERROR_NONE &&
		file_matches(named, saved_file.data, saved_file.size));

	/*
	 * Force a genuine short write. The old file and retained identity must
	 * survive, with no leftover private file. A later retry must succeed.
	 */
	CHECK(getrlimit(RLIMIT_FSIZE, &limit) == 0);
	small = limit;
	small.rlim_cur = 16;
	handler = signal(SIGXFSZ, SIG_IGN);
	CHECK(handler != SIG_ERR && setrlimit(RLIMIT_FSIZE, &small) == 0);
	CHECK(mh_league_store_save(store, "Player.lGf", &document, NULL) !=
		MH_ERROR_NONE);
	CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0 &&
		signal(SIGXFSZ, handler) != SIG_ERR);
	CHECK(file_matches(named, saved_file.data, saved_file.size) &&
		directory_entries(user) == 2);
	TRY(mh_league_store_save(store, "Player.lGf", &document, &error));

	/*
	 * Replaced and newly created destinations are not implicit overwrites.
	 */
	CHECK(rename(named, held) == 0 && write_text(named, "replacement"));
	CHECK(mh_league_store_save(store, "Player.lGf", &document, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_league_store_remove(store, "Player.lGf", NULL) !=
		MH_ERROR_NONE);
	CHECK(file_matches(named, (const unsigned char *)"replacement", 11));
	CHECK(unlink(named) == 0 && rename(held, named) == 0);
	CHECK(write_text(other, "new file"));
	CHECK(mh_league_store_save(store, "other.LGF", &document, NULL) !=
			MH_ERROR_NONE &&
		mh_league_store_remove(store, "other.LGF", NULL) !=
			MH_ERROR_NONE);
	CHECK(file_matches(other, (const unsigned char *)"new file", 8) &&
		unlink(other) == 0);
	join_path(other, user, "PLAYER.LGF");
	CHECK(write_text(other, "case alias"));
	CHECK(mh_league_store_save(store, "Player.lGf", &document, NULL) !=
			MH_ERROR_NONE &&
		mh_league_store_remove(store, "Player.lGf", NULL) !=
			MH_ERROR_NONE);
	CHECK(mh_league_store_open(assets, user, document.profiles, 26, &empty,
		&fresh, NULL) != MH_ERROR_NONE);
	CHECK(empty.entries == NULL && fresh == NULL && unlink(other) == 0);

	/* Original assets and their directory aliases are always excluded. */
	join_path(other, assets, "Original.LGF");
	TRY(mh_league_file_save(other, &document, &error));
	TRY(mh_file_read(other, &original_file, &error));
	CHECK(symlink(assets, alias) == 0);
	CHECK(mh_league_store_open(assets, assets, document.profiles, 26,
		&empty, &fresh, NULL) != MH_ERROR_NONE);
	CHECK(mh_league_store_open(assets, alias, document.profiles, 26, &empty,
		&fresh, NULL) != MH_ERROR_NONE);
	CHECK(rename(user, moved) == 0 && symlink(assets, user) == 0);
	CHECK(mh_league_store_save(store, "Original.LGF", &document, NULL) !=
			MH_ERROR_NONE &&
		mh_league_store_remove(store, "Player.lGf", NULL) !=
			MH_ERROR_NONE);
	CHECK(file_matches(other, original_file.data, original_file.size));
	CHECK(unlink(user) == 0 && rename(moved, user) == 0);
	join_path(other, user, "Link.LGF");
	CHECK(symlink(named, other) == 0);
	CHECK(mh_league_store_open(assets, user, document.profiles, 26, &empty,
		&fresh, NULL) != MH_ERROR_NONE);
	CHECK(mh_league_store_save(store, "Link.LGF", &document, NULL) !=
			MH_ERROR_NONE &&
		unlink(other) == 0);
	join_path(other, user, "Pipe.LGF");
	CHECK(mkfifo(other, 0600) == 0);
	CHECK(mh_league_store_open(assets, user, document.profiles, 26, &empty,
		&fresh, NULL) != MH_ERROR_NONE &&
		unlink(other) == 0);
	join_path(other, user, "Folder.LGF");
	CHECK(mkdir(other, 0700) == 0);
	CHECK(mh_league_store_open(assets, user, document.profiles, 26, &empty,
		&fresh, NULL) != MH_ERROR_NONE &&
		rmdir(other) == 0);

	/* Malformed and oversized saves cannot partially publish a catalog. */
	join_path(other, user, "zBad.LGF");
	CHECK(write_text(other, "bad"));
	CHECK(mh_league_store_open(assets, user, document.profiles, 26, &empty,
		&fresh, NULL) != MH_ERROR_NONE);
	CHECK(empty.entries == NULL && fresh == NULL && unlink(other) == 0);
	CHECK(write_text(other, "large") && truncate(other, 65541) == 0);
	CHECK(mh_league_store_open(assets, user, document.profiles, 26, &empty,
		&fresh, NULL) != MH_ERROR_NONE);
	CHECK(empty.entries == NULL && fresh == NULL && unlink(other) == 0);
	TRY(mh_league_store_save(store, "aFirst.LGF", &document, &error));
	TRY(mh_league_store_open(
		assets, user, document.profiles, 26, &empty, &fresh, &error));
	CHECK(empty.count == 2 &&
		strcmp(empty.entries[0].filename, "aFirst.LGF") == 0 &&
		strcmp(empty.entries[1].filename, "Player.lGf") == 0);
	CHECK(empty.entries[0].dirty == 0 &&
		empty.entries[1].document.league.race_index == 7);
	TRY(mh_league_catalog_restore(&empty, "SJ\366N", &error));
	CHECK(empty.selected == 1);

	/*
	 * Two stores loaded the same inode: only the first successful
	 * replacement may proceed without an explicit reload in the other
	 * store.
	 */
	TRY(mh_league_store_save(store, "Player.lGf", &document, &error));
	CHECK(mh_league_store_save(fresh, "Player.lGf", &document, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_league_store_remove(fresh, "Player.lGf", NULL) !=
		MH_ERROR_NONE);
	mh_league_store_close(fresh);
	mh_league_catalog_close(&empty);
	fresh = NULL;
	TRY(mh_league_store_remove(store, "aFirst.LGF", &error));
	TRY(mh_league_store_remove(store, "Player.lGf", &error));
	CHECK(directory_entries(user) == 1);

	/* Removed names can be reused without carrying stale identity state. */
	TRY(mh_league_store_save(store, "Player.lGf", &document, &error));
	TRY(mh_league_store_remove(store, "Player.lGf", &error));
	TRY(mh_league_store_save(store, "InPlace.LGF", &document, &error));
	join_path(other, user, "InPlace.LGF");
	CHECK(write_text(other, "modified in place"));
	CHECK(mh_league_store_save(store, "InPlace.LGF", &document, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_league_store_remove(store, "InPlace.LGF", NULL) !=
		MH_ERROR_NONE);
	CHECK(file_matches(
		other, (const unsigned char *)"modified in place", 17) &&
		unlink(other) == 0);
	mh_league_store_close(store);
	mh_league_catalog_close(&catalog);
	free(saved_file.data);
	free(original_file.data);
	join_path(other, assets, "Original.LGF");
	CHECK(unlink(other) == 0 && unlink(alias) == 0 && unlink(ignored) == 0);
	CHECK(rmdir(assets) == 0 && rmdir(user) == 0 && rmdir(temporary) == 0);
	puts("League store: exact TBL keys, bounded loads, atomic writes, stale files, aliases, assets and deletion passed");

	return 0;
}
