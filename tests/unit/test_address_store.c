#define _XOPEN_SOURCE 700
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "client/diagnostics/storage_address_store.h"
#include "engine/core/error.h"
#include "engine/storage/address_store.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Address store line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
write_file(const char *directory, const char *name, const char *text)
{
	char path[512];
	FILE *file;
	int success;

	if (strlen(directory) + strlen(name) + 2 > sizeof(path))
		return 0;

	strcpy(path, directory);
	strcat(path, "/");
	strcat(path, name);
	file = fopen(path, "wb");

	if (file == NULL)
		return 0;

	success = fputs(text, file) >= 0;

	if (fclose(file) != 0)
		success = 0;

	return success;
}

static int
no_pending_files(const char *path)
{
	DIR *directory;
	struct dirent *entry;

	directory = opendir(path);

	if (directory == NULL)
		return 0;

	while ((entry = readdir(directory)) != NULL)
		if (strncmp(entry->d_name, ".mh-data-", 9) == 0) {
			closedir(directory);
			return 0;
		}

	return closedir(directory) == 0;
}

int
main(void)
{
	char *root;
	char assets[128], directory[128], alias[128], path[160], moved[128];
	struct mh_address_store *store, *unchanged;
	const struct mh_address_book *book;
	struct mh_address_entry snapshot[4];
	struct rlimit limit;
	pid_t child;
	int status;

	CHECK((root = mh_test_directory("mh-addresses-")) != NULL);
	strcpy(assets, root);
	strcat(assets, "/assets");
	CHECK(mkdir(assets, 0700) == 0);
	strcpy(directory, root);
	strcat(directory, "/AddressBook");
	CHECK(mkdir(directory, 0700) == 0);
	strcpy(alias, root);
	strcat(alias, "/alias");
	CHECK(symlink(assets, alias) == 0);
	unchanged = NULL;
	CHECK(mh_address_store_open(assets, assets, &unchanged, NULL) !=
			MH_ERROR_NONE &&
		unchanged == NULL);
	CHECK(mh_address_store_open(alias, assets, &unchanged, NULL) !=
			MH_ERROR_NONE &&
		unchanged == NULL);
	CHECK(write_file(directory, "Alpha.NPC", "Type IPX\n"));
	CHECK(write_file(
		directory, "beta.npc", "Type TCP\nAddress host.example\n"));
	CHECK(mh_address_store_open(directory, assets, &store, NULL) ==
		MH_ERROR_NONE);
	book = mh_address_store_book(store);
	CHECK(book->count == 2 &&
		strcmp(book->entries[1].profile.name, "TCP/IP_03") == 0);
	CHECK(mh_address_store_add(store, "Alpha", "127.0.0.1", NULL) ==
		MH_ERROR_NONE);
	CHECK(book->count == 3 &&
		strcmp(book->entries[1].filename, "alpha!.npc") == 0);
	CHECK(mh_address_store_add(store, "Alpha", "game.example", NULL) ==
		MH_ERROR_NONE);
	CHECK(book->count == 4 &&
		strcmp(book->entries[2].filename, "alpha!!.npc") == 0 &&
		no_pending_files(directory));
	CHECK(mh_address_store_remove(store, 0, NULL) != MH_ERROR_NONE &&
		book->count == 4);
	memcpy(snapshot, book->entries, sizeof(snapshot));

	/*
	 * A short disk write must not publish or mutate the in-memory catalog.
	 */
	child = fork();
	CHECK(child >= 0);

	if (child == 0) {
		signal(SIGXFSZ, SIG_IGN);
		limit.rlim_cur = limit.rlim_max = 16;

		if (setrlimit(RLIMIT_FSIZE, &limit) != 0)
			_exit(2);

		_exit(mh_address_store_add(store, "Short", "ip", NULL) !=
						MH_ERROR_NONE &&
					book->count == 4 &&
					memcmp(snapshot, book->entries,
						sizeof(snapshot)) == 0
				? 0
				: 3);
	}

	CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
		WEXITSTATUS(status) == 0);
	CHECK(no_pending_files(directory));
	strcpy(path, directory);
	strcat(path, "/Short.NPC");
	CHECK(access(path, F_OK) != 0);
	CHECK(write_file(directory, "beta.npc",
		"Type TCP\nAddress changed.example\nDescription changed\n"));
	CHECK(mh_address_store_remove(store, 3, NULL) != MH_ERROR_NONE &&
		book->count == 4);
	CHECK(memcmp(snapshot, book->entries, sizeof(snapshot)) == 0);
	CHECK(mh_address_store_remove(store, 1, NULL) == MH_ERROR_NONE &&
		book->count == 3);
	CHECK(strcmp(book->entries[1].filename, "alpha!!.npc") == 0);
	memcpy(snapshot, book->entries, 3 * sizeof(*snapshot));
	CHECK(mh_address_store_reload(NULL, NULL) != MH_ERROR_NONE);
	CHECK(write_file(directory, "ALPHA.npc", "Type TCP\n"));
	CHECK(mh_address_store_reload(store, NULL) != MH_ERROR_NONE &&
		book->count == 3);
	CHECK(memcmp(snapshot, book->entries, 3 * sizeof(*snapshot)) == 0);
	strcpy(path, directory);
	strcat(path, "/ALPHA.npc");
	CHECK(unlink(path) == 0);
	strcpy(path, directory);
	strcat(path, "/link.npc");
	CHECK(symlink("beta.npc", path) == 0);
	CHECK(mh_address_store_reload(store, NULL) != MH_ERROR_NONE &&
		book->count == 3);
	CHECK(memcmp(snapshot, book->entries, 3 * sizeof(*snapshot)) == 0);
	CHECK(unlink(path) == 0);
	CHECK(mh_address_store_reload(store, NULL) == MH_ERROR_NONE);
	book = mh_address_store_book(store);
	CHECK(book->count == 3 &&
		strcmp(book->entries[1].filename, "alpha!!.npc") == 0);
	CHECK(strcmp(book->entries[1].profile.ip, "game.example") == 0 &&
		strcmp(book->entries[2].profile.name, "changed") == 0);
	CHECK(write_file(directory, "Added.npc",
		"Type TCP\nAddress added.example\nDescription Added\n"));
	CHECK(mh_address_store_reload(store, NULL) == MH_ERROR_NONE);
	book = mh_address_store_book(store);
	CHECK(book->count == 4 &&
		strcmp(book->entries[1].profile.name, "Added") == 0);
	CHECK(strcmp(book->entries[1].profile.ip, "added.example") == 0);
	strcpy(path, directory);
	strcat(path, "/Added.npc");
	CHECK(unlink(path) == 0);
	CHECK(mh_address_store_reload(store, NULL) == MH_ERROR_NONE &&
		book->count == 3 && no_pending_files(directory));
	strcpy(moved, root);
	strcat(moved, "/moved");
	CHECK(rename(directory, moved) == 0 && mkdir(directory, 0700) == 0);
	CHECK(mh_address_store_add(store, "Elsewhere", "ip", NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_address_store_remove(store, 1, NULL) != MH_ERROR_NONE &&
		book->count == 3);
	CHECK(mh_address_store_reload(store, NULL) != MH_ERROR_NONE &&
		book->count == 3);
	CHECK(rmdir(directory) == 0 && rename(moved, directory) == 0);
	CHECK(mh_address_store_remove(store, 1, NULL) == MH_ERROR_NONE &&
		mh_address_store_remove(store, 1, NULL) == MH_ERROR_NONE);
	mh_address_store_close(store);
	CHECK(write_file(directory, "ALPHA.npc", "Type TCP"));
	CHECK(mh_address_store_open(directory, assets, &unchanged, NULL) !=
			MH_ERROR_NONE &&
		unchanged == NULL);
	strcpy(path, directory);
	strcat(path, "/ALPHA.npc");
	CHECK(unlink(path) == 0);
	strcpy(path, directory);
	strcat(path, "/link.npc");
	CHECK(symlink("Alpha.NPC", path) == 0);
	CHECK(mh_address_store_open(directory, assets, &unchanged, NULL) !=
			MH_ERROR_NONE &&
		unchanged == NULL);
	CHECK(unlink(path) == 0);
	strcpy(path, directory);
	strcat(path, "/Alpha.NPC");
	CHECK(unlink(path) == 0);
	CHECK(unlink(alias) == 0 && rmdir(directory) == 0 &&
		rmdir(assets) == 0 && rmdir(root) == 0);

	return 0;
}
