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
#include "client/diagnostics/storage_demo_file.h"
#include "engine/core/error.h"
#include "engine/storage/demo_file.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "recording save line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)
#define TRY(x)                                                                 \
	do {                                                                   \
		enum mh_error_code status;                                     \
                                                                               \
		status = (x);                                                  \
                                                                               \
		if (status != MH_ERROR_NONE) {                                 \
			fprintf(stderr, "recording save line %d: %s\n",        \
				__LINE__, error.message);                      \
			return 0;                                              \
		}                                                              \
	} while (0)

static int
make_path(char output[256], const char *directory, const char *name)
{
	size_t directory_length, name_length;

	directory_length = strlen(directory);
	name_length = strlen(name);
	CHECK(directory_length < 255 && name_length < 255 - directory_length);
	memcpy(output, directory, directory_length);
	output[directory_length] = '/';
	memcpy(output + directory_length + 1, name, name_length + 1);

	return 1;
}

static int
write_text(const char *name, const char *text)
{
	FILE *stream;

	stream = fopen(name, "wb");
	CHECK(stream != NULL);
	CHECK(fputs(text, stream) >= 0 && fclose(stream) == 0);

	return 1;
}

static int
check_contents(const char *name, const char *text)
{
	struct mh_error error;
	struct mh_file file;

	TRY(mh_file_read(name, &file, &error));
	CHECK(file.size == strlen(text) &&
		memcmp(file.data, text, file.size) == 0);
	free(file.data);

	return 1;
}

static int
check_recording(const char *name, unsigned int identity)
{
	struct mh_error error;
	struct mh_file file;
	struct mh_demo demo;
	struct stat info;

	TRY(mh_file_read(name, &file, &error));
	TRY(mh_demo_read(file.data, file.size, &demo, &error));
	CHECK(demo.player_count == 1 && demo.frame_count >= 64 &&
		demo.players[0].crossings[1][2] == identity);
	CHECK(stat(name, &info) == 0 && (info.st_mode & 0777) == 0600 &&
		info.st_nlink == 1);
	free(file.data);

	return 1;
}

static int
make_take(const char *name, unsigned int identity, int seal,
	struct mh_demo_file **file, struct mh_demo *header, size_t *frames)
{
	struct mh_error error;
	struct mh_demo_recorder writer;
	struct mh_demo_record_sample sample;
	struct mh_demo_record_event event;
	unsigned int i;

	memset(header, 0, sizeof(*header));
	memset(&sample, 0, sizeof(sample));
	header->player_count = header->laps = header->players[0].active =
		sample.active_mask = 1;
	strcpy(header->track, "RedRock");
	TRY(mh_demo_record_begin(header, &writer, &error));
	TRY(mh_demo_file_open(name, header, file, &error));
	TRY(mh_demo_record_step(&writer, 1000, &sample, &event, &error));

	for (i = 1; i <= 65; i++) {
		TRY(mh_demo_record_step(
			&writer, 1000 + i * 63, &sample, &event, &error));
		TRY(mh_demo_file_append(*file, &event, &error));
	}

	header->players[0].crossings[1][2] = identity;
	TRY(mh_demo_record_finish(
		&writer, &sample, header->players, 1, &event, &error));
	TRY(mh_demo_file_append(*file, &event, &error));
	*header = writer.header;
	*frames = writer.frame_count;

	if (seal)
		TRY(mh_demo_file_seal(*file, header, *frames, &error));

	CHECK(access(name, F_OK) != 0);

	return 1;
}

static int
save_choices(const char *directory, const char *assets)
{
	struct mh_error error;
	struct mh_demo_file *file;
	struct mh_demo header;
	struct mh_demo_record_event event;
	char initial[256], named[256], alias[256], held[256];
	static const unsigned char *invalid[] = {(const unsigned char *)"",
		(const unsigned char *)"../other",
		(const unsigned char *)"..\\other", (const unsigned char *)".",
		(const unsigned char *)"..", (const unsigned char *)"trailing ",
		(const unsigned char *)"trailing.",
		(const unsigned char *)"bad:name",
		(const unsigned char *)"bad\nname",
		(const unsigned char *)"bad*name",
		(const unsigned char *)"012345678901234567890123"};

	size_t frames;
	unsigned int i;
	int exists;

	CHECK(make_path(initial, directory, "take.MDE") &&
		make_path(named, directory, "friday race.mde"));
	CHECK(make_path(alias, directory, "FRIDAY RACE.mde") &&
		make_path(held, directory, "held"));
	CHECK(make_take(initial, 1, 0, &file, &header, &frames));
	exists = 42;
	CHECK(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists,
		NULL) != MH_ERROR_NONE &&
		exists == 42);
	CHECK(mh_demo_file_publish(file, NULL) != MH_ERROR_NONE);
	TRY(mh_demo_file_seal(file, &header, frames, &error));
	memset(&event, 0, sizeof(event));
	CHECK(mh_demo_file_append(file, &event, NULL) != MH_ERROR_NONE);
	CHECK(mh_demo_file_save(file, 0, NULL) != MH_ERROR_NONE);
	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists, &error));
	CHECK(!exists);

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		exists = 42;
		CHECK(mh_demo_file_probe(file, assets, invalid[i], &exists,
			NULL) != MH_ERROR_NONE &&
			exists == 42);
		CHECK(mh_demo_file_save(file, 0, NULL) !=
			MH_ERROR_NONE); /* Failed probes invalidate older
					choices. */
	}

	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists, &error));
	CHECK(!exists);
	CHECK(mh_demo_file_save(file, 1, NULL) != MH_ERROR_NONE);
	TRY(mh_demo_file_save(file, 0, &error));
	CHECK(mh_demo_file_save(file, 0, NULL) != MH_ERROR_NONE);
	mh_demo_file_abort(file);
	CHECK(check_recording(named, 1) && access(initial, F_OK) != 0);

	CHECK(make_take(initial, 2, 1, &file, &header, &frames));
	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"FRIDAY RACE", &exists, &error));
	CHECK(exists);
	CHECK(mh_demo_file_save(file, 0, NULL) != MH_ERROR_NONE);
	CHECK(check_recording(named, 1));

	/*
	 * Cancel the overwrite and choose a different name, then cancel the
	 * take.
	 */
	TRY(mh_demo_file_probe(file, assets, (const unsigned char *)"Another",
		&exists, &error));
	CHECK(!exists);
	mh_demo_file_abort(file);
	CHECK(check_recording(named, 1));
	CHECK(make_take(initial, 2, 1, &file, &header, &frames));
	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"FRIDAY RACE", &exists, &error));
	CHECK(exists);

	/*
	 * The overwrite choice follows the inode, even after an in-place edit.
	 */
	CHECK(write_text(named, "same entry, edited contents"));
	TRY(mh_demo_file_save(file, 1, &error));
	mh_demo_file_abort(file);
	CHECK(check_recording(named, 2) &&
		access(alias, F_OK) != 0); /* Retain existing spelling. */

	CHECK(make_take(initial, 3, 1, &file, &header, &frames));
	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists, &error));
	CHECK(rename(named, held) == 0 && write_text(named, "new owner"));
	CHECK(mh_demo_file_save(file, 1, NULL) != MH_ERROR_NONE &&
		check_contents(named, "new owner"));
	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists, &error));
	CHECK(exists);
	TRY(mh_demo_file_save(file, 1, &error));
	mh_demo_file_abort(file);
	CHECK(check_recording(named, 3) && check_recording(held, 2));
	CHECK(unlink(held) == 0);

	CHECK(make_take(initial, 4, 1, &file, &header, &frames));
	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists, &error));
	CHECK(rename(named, alias) == 0);
	CHECK(mh_demo_file_save(file, 1, NULL) != MH_ERROR_NONE &&
		check_recording(alias, 3));
	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists, &error));
	CHECK(exists);
	CHECK(write_text(named, "ambiguous"));
	CHECK(mh_demo_file_save(file, 1, NULL) != MH_ERROR_NONE &&
		check_contents(named, "ambiguous") &&
		check_recording(alias, 3));
	exists = 42;
	CHECK(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists,
		NULL) != MH_ERROR_NONE &&
		exists == 42);
	CHECK(unlink(named) == 0 && unlink(alias) == 0);
	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists, &error));
	CHECK(!exists);
	CHECK(write_text(alias, "appeared during prompt"));
	CHECK(mh_demo_file_save(file, 0, NULL) != MH_ERROR_NONE &&
		check_contents(alias, "appeared during prompt"));
	CHECK(unlink(alias) == 0);
	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists, &error));
	CHECK(symlink(initial, named) ==
		0); /* Dangling symlink is still occupied. */
	CHECK(mh_demo_file_save(file, 0, NULL) != MH_ERROR_NONE);
	CHECK(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists,
		NULL) != MH_ERROR_NONE);
	CHECK(unlink(named) == 0 && mkdir(named, 0700) == 0);
	CHECK(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists,
		NULL) != MH_ERROR_NONE);
	CHECK(rmdir(named) == 0);
	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Friday race", &exists, &error));
	TRY(mh_demo_file_save(file, 0, &error));
	mh_demo_file_abort(file);
	CHECK(check_recording(named, 4));
	CHECK(unlink(named) == 0);

	/* Original byte text is retained, including Swedish ANSI characters. */
	CHECK(make_take(initial, 5, 1, &file, &header, &frames));
	CHECK(make_path(named, directory, "r\344ksm\366rg\345s.mde"));
	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"R\344ksm\366rg\345s", &exists, &error));
	TRY(mh_demo_file_save(file, 0, &error));
	mh_demo_file_abort(file);
	CHECK(check_recording(named, 5));
	CHECK(unlink(named) == 0);
	CHECK(make_take(initial, 6, 1, &file, &header, &frames));
	CHECK(make_path(named, directory, "01234567890123456789012.mde"));
	TRY(mh_demo_file_probe(file, assets,
		(const unsigned char *)"01234567890123456789012", &exists,
		&error));
	TRY(mh_demo_file_save(file, 0, &error));
	mh_demo_file_abort(file);
	CHECK(check_recording(named, 6));
	CHECK(unlink(named) == 0);

	return 1;
}

static int
save_ownership(const char *base, const char *directory, const char *assets)
{
	struct mh_error error;
	struct mh_demo_file *file;
	struct mh_demo header;
	struct dirent *item;
	struct rlimit limit, restricted;
	void (*handler)(int);
	DIR *stream;
	char initial[256], named[256], moved[256], held[256], temporary[256];
	size_t frames;
	int exists;

	CHECK(make_path(initial, directory, "take.MDE") &&
		make_path(named, directory, "race.mde"));
	CHECK(make_path(moved, base, "moved") &&
		make_path(held, directory, "held"));
	CHECK(make_take(initial, 1, 1, &file, &header, &frames));
	TRY(mh_demo_file_probe(
		file, assets, (const unsigned char *)"Race", &exists, &error));
	CHECK(rename(directory, moved) == 0 && mkdir(directory, 0700) == 0 &&
		write_text(named, "new directory"));
	CHECK(mh_demo_file_save(file, 0, NULL) != MH_ERROR_NONE &&
		check_contents(named, "new directory"));
	mh_demo_file_abort(file);
	CHECK(rmdir(moved) == 0); /* Cleanup uses the retained directory. */
	CHECK(unlink(named) == 0);

	CHECK(make_take(initial, 2, 1, &file, &header, &frames));
	TRY(mh_demo_file_probe(
		file, assets, (const unsigned char *)"Race", &exists, &error));
	temporary[0] = 0;
	stream = opendir(directory);
	CHECK(stream != NULL);

	while ((item = readdir(stream)) != NULL)
		if (strncmp(item->d_name, ".mh-data-", 9) == 0)
			CHECK(make_path(temporary, directory, item->d_name));

	CHECK(closedir(stream) == 0 && temporary[0] != 0);
	CHECK(rename(temporary, held) == 0 &&
		write_text(temporary, "foreign temporary"));
	CHECK(mh_demo_file_save(file, 0, NULL) != MH_ERROR_NONE &&
		mh_demo_file_publish(file, NULL) != MH_ERROR_NONE);
	mh_demo_file_abort(file);
	CHECK(check_contents(temporary, "foreign temporary") &&
		check_recording(held, 2));
	CHECK(unlink(temporary) == 0 && unlink(held) == 0);

	CHECK(make_take(initial, 3, 0, &file, &header, &frames));
	CHECK(getrlimit(RLIMIT_FSIZE, &limit) == 0);
	restricted = limit;
	restricted.rlim_cur = 0;
	handler = signal(SIGXFSZ, SIG_IGN);
	CHECK(handler != SIG_ERR);
	CHECK(setrlimit(RLIMIT_FSIZE, &restricted) == 0);
	exists =
		mh_demo_file_seal(file, &header, frames, NULL) != MH_ERROR_NONE;
	CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0 &&
		signal(SIGXFSZ, handler) != SIG_ERR);
	CHECK(exists && mh_demo_file_publish(file, NULL) != MH_ERROR_NONE);
	CHECK(mh_demo_file_probe(file, assets, (const unsigned char *)"Race",
		&exists, NULL) != MH_ERROR_NONE);
	mh_demo_file_abort(file);
	CHECK(access(initial, F_OK) != 0 && access(named, F_OK) != 0);

	return 1;
}

static int
protected_assets(const char *base, const char *assets)
{
	struct mh_demo_file *file;
	struct mh_demo header;
	char initial[256], named[256], alias[256];
	size_t frames;
	int exists;

	CHECK(make_path(alias, base, "asset-alias") &&
		symlink(assets, alias) == 0);
	CHECK(make_path(initial, alias, "take.MDE") &&
		make_path(named, assets, "Original.MDE"));
	CHECK(write_text(named, "original asset"));
	CHECK(make_take(initial, 1, 1, &file, &header, &frames));
	exists = 42;
	CHECK(mh_demo_file_probe(file, assets,
		(const unsigned char *)"Original", &exists,
		NULL) != MH_ERROR_NONE &&
		exists == 42);
	CHECK(mh_demo_file_probe(file, assets, (const unsigned char *)"New",
		&exists, NULL) != MH_ERROR_NONE);
	CHECK(mh_demo_file_save(file, 1, NULL) != MH_ERROR_NONE);
	mh_demo_file_abort(file);
	CHECK(check_contents(named, "original asset"));
	CHECK(unlink(named) == 0 && unlink(alias) == 0);

	return 1;
}

static int
network_recording(const char *base, const char *directory, const char *assets)
{
	struct mh_error error;
	struct mh_demo_file *file;
	struct mh_demo header;
	struct mh_demo_record_event event;
	char name[256], alias[256], protected[256];

	memset(&header, 0, sizeof(header));
	header.player_count = header.laps = header.players[0].active = 1;
	file = NULL;
	CHECK(make_path(name, assets, "new.MDE"));
	CHECK(mh_demo_file_open_user(name, assets, &header, &file, NULL) !=
			MH_ERROR_NONE &&
		file == NULL);
	CHECK(make_path(alias, base, "linked-assets") &&
		symlink(assets, alias) == 0);
	CHECK(make_path(protected, alias, "new.MDE"));
	CHECK(mh_demo_file_open_user(protected, assets, &header, &file, NULL) !=
			MH_ERROR_NONE &&
		file == NULL);
	CHECK(unlink(alias) == 0);
	CHECK(make_path(name, directory, "Network.MDE") &&
		make_path(alias, directory, "nEtWoRk.mde"));
	CHECK(write_text(alias, "existing take"));
	CHECK(mh_demo_file_open_user(name, assets, &header, &file, NULL) !=
			MH_ERROR_NONE &&
		file == NULL);
	CHECK(check_contents(alias, "existing take") && unlink(alias) == 0);
	TRY(mh_demo_file_open_user(name, assets, &header, &file, &error));
	memset(&event, 0, sizeof(event));
	event.size = 32 * 38;
	TRY(mh_demo_file_append(file, &event, &error));
	TRY(mh_demo_file_append(file, &event, &error));
	TRY(mh_demo_file_seal(file, &header, 64, &error));
	CHECK(write_text(alias, "late collision"));
	CHECK(mh_demo_file_publish(file, NULL) != MH_ERROR_NONE &&
		check_contents(alias, "late collision"));
	CHECK(unlink(alias) == 0);
	TRY(mh_demo_file_publish(file, &error));
	mh_demo_file_abort(file);
	CHECK(unlink(name) == 0);

	return 1;
}

int
main(void)
{
	char *base, assets[256], directory[256];
	int success;

	if ((base = mh_test_directory("mh-save-storage-")) == NULL)
		return 1;

	if (!make_path(assets, base, "assets") ||
		!make_path(directory, base, "assets-user"))
		return 1;

	if (mkdir(assets, 0700) != 0 || mkdir(directory, 0700) != 0)
		return 1;

	success = save_choices(directory, assets) &&
		save_ownership(base, directory, assets) &&
		protected_assets(base, assets) &&
		network_recording(base, directory, assets);

	if (rmdir(assets) != 0 || rmdir(directory) != 0 || rmdir(base) != 0) {
		fprintf(stderr, "recording save fixtures remain in %s\n", base);
		success = 0;
	}

	if (success)
		puts("recording save: private sealing, naming, retry, replacement, asset protection and cleanup passed");

	return success ? 0 : 1;
}
