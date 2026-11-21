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
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_control_store.h"
#include "engine/core/error.h"
#include "engine/storage/control_store.h"
#include "interfaces/storage/data_directory_backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Control store line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)
#define TRY(x)                                                                 \
	do {                                                                   \
		enum mh_error_code status;                                     \
		status = (x);                                                  \
		if (status != MH_ERROR_NONE) {                                 \
			fprintf(stderr, "Control store line %d: %s\n",         \
				__LINE__, error.message);                      \
			return 1;                                              \
		}                                                              \
	} while (0)

static void
make_path(char output[256], const char *base, const char *name)
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
file_matches(const char *name, const unsigned char *text, size_t size)
{
	struct mh_file file;
	int okay;

	if (mh_file_read(name, &file, NULL) != MH_ERROR_NONE)
		return 0;

	okay = file.size == size && memcmp(file.data, text, size) == 0;
	free(file.data);

	return okay;
}

static unsigned int
count_entries(const char *directory)
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

static void
close_catalog(
	struct mh_control_store **store, struct mh_control_catalog *catalog)
{
	mh_control_store_close(*store);

	*store = NULL;
	mh_control_catalog_free(catalog);
}

static int
check_reload(void)
{
	struct mh_error error;
	static const char source[] = "LayoutName Keyboard\nTurnLeft Q\n";
	char *temporary;
	char assets[256], user[256], original[256], saved[256], added[256];
	struct mh_control_store *store, *previous;
	struct mh_control_catalog catalog, before;
	struct mh_control_profile profiles[2];
	unsigned char keep[2];

	CHECK((temporary = mh_test_directory("mh-control-reload-")) != NULL);
	make_path(assets, temporary, "assets");
	make_path(user, temporary, "Controls");
	make_path(original, assets, "Keyboard.clo");
	make_path(saved, user, "keyboard.clo");
	make_path(added, user, "000New.clo");
	CHECK(mkdir(assets, 0700) == 0 && mkdir(user, 0700) == 0 &&
		write_text(original, source));
	store = NULL;
	memset(&catalog, 0, sizeof(catalog));
	TRY(mh_control_store_open(
		assets, assets, user, "Keyboard", &catalog, &store, &error));

	/*
	 * The original writer uses the low byte; the next reader must observe
	 * MouseButtonMiddle rather than retaining the unsaved full word.
	 */
	catalog.profiles[0].layout.bindings[0] = MH_CONTROL_UNBOUND;
	catalog.profiles[0].dirty = 1;
	TRY(mh_control_store_save(store, &catalog, &error));
	CHECK(catalog.profiles[0].layout.bindings[0] == MH_CONTROL_UNBOUND);
	TRY(mh_control_store_reload(
		&store, catalog.profiles[0].layout.name, &catalog, &error));
	CHECK(catalog.count == 1 && catalog.selected == 0 &&
		catalog.profiles[0].dirty == 0 &&
		catalog.profiles[0].layout.bindings[0] == 0xff);
	CHECK(write_text(saved, "LayoutName Keyboard\nTurnLeft Z\n"));
	CHECK(write_text(added, "LayoutName Reloaded\nInCarView R\n"));
	TRY(mh_control_store_reload(&store, "reLOADED", &catalog, &error));
	CHECK(catalog.count == 2 && catalog.selected == 0 &&
		catalog.profiles[1].layout.bindings[0] == 0x2c &&
		catalog.profiles[0].layout.bindings[MH_CONTROL_IN_CAR] == 0x13);

	/*
	 * Filtering retains file identities and never deletes excluded saves.
	 */
	keep[0] = keep[1] = 0;
	CHECK(mh_control_store_restrict(store, &catalog, keep, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(catalog.count == 2);
	keep[1] = 1;
	TRY(mh_control_store_restrict(store, &catalog, keep, &error));
	CHECK(catalog.count == 1 && catalog.selected == 0 &&
		strcmp(catalog.profiles[0].layout.name, "Keyboard") == 0);
	catalog.profiles[0].layout.bindings[0] = 0x2c;
	catalog.profiles[0].dirty = 1;
	TRY(mh_control_store_save(store, &catalog, &error));
	TRY(mh_control_store_reload(&store, "reLOADED", &catalog, &error));
	CHECK(catalog.count == 2 && catalog.selected == 0 &&
		catalog.profiles[1].layout.bindings[0] == 0x2c);
	before = catalog;
	previous = store;
	memcpy(profiles, catalog.profiles, sizeof(profiles));
	CHECK(write_text(added, "MouseSpeedH 1e99999\n"));
	CHECK(mh_control_store_reload(&store, "Keyboard", &catalog, NULL) !=
		MH_ERROR_NONE);
	CHECK(store == previous && catalog.profiles == before.profiles &&
		catalog.count == before.count &&
		catalog.selected == before.selected &&
		memcmp(profiles, catalog.profiles, sizeof(profiles)) == 0);
	CHECK(write_text(added, "LayoutName Reloaded\nInCarView T\n"));
	TRY(mh_control_store_reload(&store, "missing", &catalog, &error));
	CHECK(catalog.selected == 0 &&
		catalog.profiles[0].layout.bindings[MH_CONTROL_IN_CAR] == 0x14);

	/*
	 * Fresh file metadata permits the next edit/save after external edits.
	 */
	catalog.profiles[0].layout.bindings[0] = 0x1e;
	catalog.profiles[0].dirty = 1;
	TRY(mh_control_store_save(store, &catalog, &error));
	CHECK(catalog.profiles[0].dirty == 0);
	CHECK(unlink(added) == 0);
	TRY(mh_control_store_reload(
		&store, catalog.profiles[0].layout.name, &catalog, &error));
	CHECK(catalog.count == 1 && catalog.selected == 0 &&
		catalog.profiles[0].layout.bindings[0] == 0x2c);
	CHECK(unlink(saved) == 0);
	TRY(mh_control_store_reload(&store, "Keyboard", &catalog, &error));
	CHECK(catalog.count == 1 &&
		catalog.profiles[0].layout.bindings[0] == 0x10);
	CHECK(file_matches(
		original, (const unsigned char *)source, sizeof(source) - 1));
	close_catalog(&store, &catalog);
	CHECK(unlink(original) == 0 && rmdir(assets) == 0 && rmdir(user) == 0 &&
		rmdir(temporary) == 0);
	puts("Control reload: lossy writer/read cycle, additions/removals, selection, atomic failure and refreshed save metadata passed");

	return 0;
}

int
main(void)
{
	struct mh_error error;
	static const char keyboard[] = "LayoutName Keyboard\nTurnLeft Q\n";
	char *temporary;
	char assets[256], user[256], moved[256], alias[256], named[256],
		other[256], held[256], original[256], name[40];
	struct mh_control_store *store, *fresh;
	struct mh_data_directory *directory;
	struct mh_control_catalog catalog, empty;
	struct mh_control_layout loaded;
	struct mh_file before;
	struct stat info;
	struct rlimit limit, small;
	void (*handler)(int);
	unsigned int i;

	CHECK(check_reload() == 0);
	CHECK((temporary = mh_test_directory("mh-control-store-")) != NULL);
	make_path(assets, temporary, "assets");
	make_path(user, temporary, "Controls");
	make_path(moved, temporary, "moved");
	make_path(alias, temporary, "alias");
	make_path(original, assets, "Keyboard.CLO");
	make_path(named, user, "keyboard.clo");
	make_path(held, user, "held");
	CHECK(mkdir(assets, 0700) == 0 && mkdir(user, 0700) == 0 &&
		write_text(original, keyboard));
	directory = NULL;
	CHECK(mh_filesystem_data_directory_open(
		moved, assets, &directory, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(directory == NULL);
	CHECK(mh_filesystem_data_directory_open(
		user, moved, &directory, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(directory == NULL);
	CHECK(mh_filesystem_data_directory_open(
		original, assets, &directory, NULL) != MH_ERROR_NONE);
	CHECK(directory == NULL);
	TRY(mh_filesystem_data_directory_open(
		user, assets, &directory, &error));
	mh_data_directory_close(directory);
	make_path(other, assets, "zEmpty.clo");
	CHECK(write_text(other, ""));
	make_path(other, user, "aFirst.clo");
	CHECK(write_text(other, "LayoutName Keyboard\n"));
	make_path(other, assets, "ignored.txt");
	CHECK(write_text(other, "ignored"));
	memset(&catalog, 0, sizeof(catalog));
	memset(&empty, 0, sizeof(empty));
	store = fresh = NULL;
	TRY(mh_control_store_open(
		assets, assets, user, "KEYBOARD", &catalog, &store, &error));
	CHECK(catalog.count == 3 && catalog.selected == 1 &&
		catalog.profiles[1].layout.bindings[0] == 0x10);
	CHECK(strcmp(catalog.profiles[2].layout.name, "Layout03") == 0 &&
		catalog.profiles[2].dirty == 0);
	TRY(mh_control_store_save(store, &catalog, &error));
	CHECK(count_entries(user) == 1);
	catalog.profiles[1].layout.bindings[0] = 0x1e;
	catalog.profiles[1].dirty = 1;
	catalog.profiles[2].dirty = 2; /* Original writer tests exactly 1. */
	TRY(mh_control_store_save(store, &catalog, &error));
	CHECK(count_entries(user) == 2 && catalog.profiles[1].dirty == 0);
	CHECK(catalog.profiles[2].dirty == 2 && stat(named, &info) == 0 &&
		(info.st_mode & 0777) == 0600 && info.st_nlink == 1);
	TRY(mh_file_read(named, &before, &error));
	TRY(mh_control_layout_read(
		before.data, before.size, NULL, &loaded, &error));
	CHECK(loaded.bindings[0] == 0x1e);
	CHECK(file_matches(original, (const unsigned char *)keyboard,
		sizeof(keyboard) - 1));

	/* All pending encodings are validated before any file is written. */
	catalog.profiles[1].dirty = catalog.profiles[2].dirty = 1;
	catalog.profiles[2].layout.name[0] = '\n';
	CHECK(mh_control_store_save(store, &catalog, NULL) != MH_ERROR_NONE &&
		catalog.profiles[1].dirty == 1);
	CHECK(file_matches(named, before.data, before.size));
	strcpy(catalog.profiles[2].layout.name, "Layout03");
	catalog.profiles[2].dirty = 0;

	/*
	 * A real short write preserves the old file and allows a successful
	 * retry.
	 */
	CHECK(getrlimit(RLIMIT_FSIZE, &limit) == 0);
	small = limit;
	small.rlim_cur = 16;
	handler = signal(SIGXFSZ, SIG_IGN);
	CHECK(handler != SIG_ERR && setrlimit(RLIMIT_FSIZE, &small) == 0);
	CHECK(mh_control_store_save(store, &catalog, NULL) != MH_ERROR_NONE);
	CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0 &&
		signal(SIGXFSZ, handler) != SIG_ERR);
	CHECK(file_matches(named, before.data, before.size) &&
		count_entries(user) == 2 && catalog.profiles[1].dirty == 1);
	TRY(mh_control_store_save(store, &catalog, &error));
	CHECK(catalog.profiles[1].dirty == 0);
	TRY(mh_control_store_open(
		assets, assets, user, "keyboard", &empty, &fresh, &error));
	CHECK(empty.count == 3 && empty.selected == 1 &&
		empty.profiles[1].layout.bindings[0] == 0x1e);
	catalog.profiles[1].dirty = 1;
	TRY(mh_control_store_save(store, &catalog, &error));
	empty.profiles[1].dirty = 1;
	CHECK(mh_control_store_save(fresh, &empty, NULL) != MH_ERROR_NONE);
	close_catalog(&fresh, &empty);

	/*
	 * Retained identities and case aliases cannot become implicit
	 * overwrites.
	 */
	catalog.profiles[1].dirty = 1;
	CHECK(rename(named, held) == 0 && write_text(named, "replacement"));
	CHECK(mh_control_store_save(store, &catalog, NULL) != MH_ERROR_NONE &&
		file_matches(named, (const unsigned char *)"replacement", 11));
	CHECK(unlink(named) == 0 && rename(held, named) == 0);
	make_path(other, user, "KEYBOARD.clo");
	CHECK(write_text(other, "case alias"));
	CHECK(mh_control_store_save(store, &catalog, NULL) != MH_ERROR_NONE);
	CHECK(mh_control_store_open(assets, assets, user, "Keyboard", &empty,
		&fresh, NULL) != MH_ERROR_NONE);
	CHECK(empty.profiles == NULL && fresh == NULL && unlink(other) == 0);
	make_path(other, user, "zEmpty.clo");
	CHECK(write_text(other, "new file"));
	catalog.profiles[2].dirty = 1;
	CHECK(mh_control_store_save(store, &catalog, NULL) != MH_ERROR_NONE &&
		file_matches(other, (const unsigned char *)"new file", 8));
	CHECK(catalog.profiles[1].dirty == 1 && unlink(other) == 0);
	catalog.profiles[2].dirty = 0;

	/*
	 * User-directory aliases and replacement with the source tree are
	 * rejected.
	 */
	CHECK(symlink(assets, alias) == 0);
	CHECK(mh_control_store_open(assets, assets, assets, "Keyboard", &empty,
		&fresh, NULL) != MH_ERROR_NONE);
	CHECK(mh_control_store_open(assets, assets, alias, "Keyboard", &empty,
		&fresh, NULL) != MH_ERROR_NONE);
	CHECK(rename(user, moved) == 0 && symlink(assets, user) == 0);
	CHECK(mh_control_store_save(store, &catalog, NULL) != MH_ERROR_NONE &&
		file_matches(original, (const unsigned char *)keyboard,
			sizeof(keyboard) - 1));
	CHECK(unlink(user) == 0 && rename(moved, user) == 0);
	TRY(mh_control_store_save(store, &catalog, &error));

	/*
	 * Symlinks, pipes and directories with a CLO extension are never read.
	 */
	make_path(other, user, "Link.clo");
	CHECK(symlink(original, other) == 0);
	CHECK(mh_control_store_open(assets, assets, user, "Keyboard", &empty,
		&fresh, NULL) != MH_ERROR_NONE &&
		unlink(other) == 0);
	CHECK(mkfifo(other, 0600) == 0);
	CHECK(mh_control_store_open(assets, assets, user, "Keyboard", &empty,
		&fresh, NULL) != MH_ERROR_NONE &&
		unlink(other) == 0);
	CHECK(mkdir(other, 0700) == 0);
	CHECK(mh_control_store_open(assets, assets, user, "Keyboard", &empty,
		&fresh, NULL) != MH_ERROR_NONE &&
		rmdir(other) == 0);
	CHECK(write_text(other, "") && truncate(other, 1048577) == 0);
	CHECK(mh_control_store_open(assets, assets, user, "Keyboard", &empty,
		&fresh, NULL) != MH_ERROR_NONE &&
		unlink(other) == 0);
	CHECK(empty.profiles == NULL && fresh == NULL);

	/*
	 * Matching user basenames override the source irrespective of spelling;
	 * an empty override still loads numbered defaults, not the source.
	 */
	close_catalog(&store, &catalog);
	make_path(other, user, "Keyboard.clo");
	CHECK(rename(named, other) == 0 && write_text(other, ""));
	TRY(mh_control_store_open(
		assets, assets, user, "Layout02", &catalog, &store, &error));
	CHECK(catalog.count == 3 && catalog.selected == 1 &&
		catalog.profiles[1].layout.bindings[0] == 0xcb);
	catalog.profiles[1].layout.bindings[0] = 0x2c;
	catalog.profiles[1].dirty = 1;
	TRY(mh_control_store_save(store, &catalog, &error));
	CHECK(access(named, F_OK) != 0 && count_entries(user) == 2);
	CHECK(write_text(other, "edited externally"));
	catalog.profiles[1].dirty = 1;
	CHECK(mh_control_store_save(store, &catalog, NULL) != MH_ERROR_NONE &&
		file_matches(
			other, (const unsigned char *)"edited externally", 17));
	close_catalog(&store, &catalog);
	CHECK(unlink(other) == 0);

	/* Installed profiles use the same bounded, no-link read policy. */
	make_path(other, assets, "Link.clo");
	CHECK(symlink(original, other) == 0);
	CHECK(mh_control_store_open(assets, assets, NULL, "Keyboard", &empty,
		&fresh, NULL) != MH_ERROR_NONE &&
		unlink(other) == 0);
	CHECK(mkfifo(other, 0600) == 0);
	CHECK(mh_control_store_open(assets, assets, NULL, "Keyboard", &empty,
		&fresh, NULL) != MH_ERROR_NONE &&
		unlink(other) == 0);
	CHECK(write_text(other, "") && truncate(other, 1048577) == 0);
	CHECK(mh_control_store_open(assets, assets, NULL, "Keyboard", &empty,
		&fresh, NULL) != MH_ERROR_NONE &&
		unlink(other) == 0);

	/* Session-only edits never create files or clear unsaved state. */
	TRY(mh_control_store_open(
		assets, assets, NULL, "Keyboard", &catalog, &store, &error));
	CHECK(catalog.count == 2 && catalog.selected == 0 &&
		catalog.profiles[0].layout.bindings[0] == 0x10);
	catalog.profiles[0].dirty = 1;
	TRY(mh_control_store_save(store, &catalog, &error));
	CHECK(catalog.profiles[0].dirty == 1 &&
		file_matches(original, (const unsigned char *)keyboard,
			sizeof(keyboard) - 1));
	close_catalog(&store, &catalog);
	make_path(other, assets, "KEYBOARD.clo");
	CHECK(write_text(other, ""));
	CHECK(mh_control_store_open(assets, assets, NULL, "Keyboard", &empty,
		&fresh, NULL) != MH_ERROR_NONE &&
		unlink(other) == 0);

	for (i = 0; i < 33; i++) {
		sprintf(name, "large%02u.clo", i);
		make_path(other, user, name);
		CHECK(write_text(other, "") && truncate(other, 1048576) == 0);
	}

	CHECK(mh_control_store_open(assets, assets, user, "Keyboard", &empty,
		&fresh, NULL) != MH_ERROR_NONE);
	CHECK(empty.profiles == NULL && fresh == NULL);

	for (i = 0; i < 33; i++) {
		sprintf(name, "large%02u.clo", i);
		make_path(other, user, name);
		CHECK(unlink(other) == 0);
	}

	free(before.data);
	CHECK(unlink(original) == 0 && unlink(alias) == 0);
	make_path(other, assets, "zEmpty.clo");
	CHECK(unlink(other) == 0);
	make_path(other, assets, "ignored.txt");
	CHECK(unlink(other) == 0);
	CHECK(mh_control_store_open(assets, assets, NULL, "Keyboard", &empty,
		&fresh, NULL) == MH_ERROR_NONE);
	CHECK(empty.count == 1 &&
		strcmp(empty.profiles[0].layout.name, "Keyboard") == 0);
	close_catalog(&fresh, &empty);
	make_path(other, user, "aFirst.clo");
	CHECK(unlink(other) == 0);
	CHECK(rmdir(assets) == 0 && rmdir(user) == 0 && rmdir(temporary) == 0);
	puts("Control profiles: source/override merge, selection, dirty saves, atomic retry, stale files, bounds and asset protection passed");

	return 0;
}
