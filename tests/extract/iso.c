#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "engine/archive/iso.h"
#include "engine/core/binary.h"

enum {
	BLOCKS = 21,
	ROOT_BLOCK = 19,
	FILE_BLOCK = 20
};

static int failures;

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

static void
write_pair(unsigned char *bytes, unsigned long value)
{
	mh_write_u32le(bytes, value);
	bytes[4] = (unsigned char)(value >> 24);
	bytes[5] = (unsigned char)(value >> 16);
	bytes[6] = (unsigned char)(value >> 8);
	bytes[7] = (unsigned char)value;
}

static void
write_record(unsigned char *bytes, unsigned long block, unsigned long size,
	const char *name)
{
	size_t length;

	length = name == NULL ? 1 : strlen(name);
	bytes[0] = (unsigned char)((33 + length + 1) & ~1U);
	write_pair(bytes + 2, block);
	write_pair(bytes + 10, size);
	bytes[25] = name == NULL ? 2 : 0;
	bytes[28] = 1;
	bytes[31] = 1;
	bytes[32] = (unsigned char)length;

	if (name != NULL)
		memcpy(bytes + 33, name, length);
}

static void
make_image(unsigned char *bytes)
{
	unsigned char *volume, *root;

	memset(bytes, 0, BLOCKS * MH_DISC_BLOCK_BYTES);
	volume = bytes + 16 * MH_DISC_BLOCK_BYTES;
	volume[0] = 1;
	memcpy(volume + 1, "CD001", 5);
	volume[6] = 1;
	write_pair(volume + 80, BLOCKS);
	volume[129] = 8;
	volume[130] = 8;
	write_record(volume + 156, ROOT_BLOCK, MH_DISC_BLOCK_BYTES, NULL);
	volume = bytes + 17 * MH_DISC_BLOCK_BYTES;
	volume[0] = 255;
	memcpy(volume + 1, "CD001", 5);
	volume[6] = 1;
	root = bytes + ROOT_BLOCK * MH_DISC_BLOCK_BYTES;
	write_record(root, ROOT_BLOCK, MH_DISC_BLOCK_BYTES, NULL);
	write_record(root + 34, FILE_BLOCK, 5, "HELLO.TXT;1");
	memcpy(bytes + FILE_BLOCK * MH_DISC_BLOCK_BYTES, "hello", 5);
}

static void
check_image(unsigned char *bytes, int valid)
{
	struct mh_resource_entry resource;
	const struct mh_resource_source *source;
	const struct mh_archive_entry *entry;
	struct mh_archive *archive;
	struct mh_file file;
	struct mh_error error;
	enum mh_error_code status;

	resource.path = "disc";
	resource.data = bytes;
	resource.size = BLOCKS * MH_DISC_BLOCK_BYTES;
	source = NULL;
	archive = NULL;
	status = mh_memory_resources_open(&resource, 1, &source, &error);

	if (status == MH_ERROR_NONE)
		status = mh_iso_open(
			source, "disc", MH_DISC_ISO, &archive, &error);

	check((status == MH_ERROR_NONE) == valid, "ISO acceptance/rejection");

	if (status == MH_ERROR_NONE) {
		entry = mh_archive_entry(archive, 0);
		check(mh_archive_count(archive) == 1 && entry != NULL &&
				strcmp(entry->path, "HELLO.TXT") == 0 &&
				entry->size == 5,
			"ISO drops version suffix and skips dot entry");
		status = mh_archive_read(archive, 0, 5, &file, &error);
		check(status == MH_ERROR_NONE, "read ISO extent");

		if (status == MH_ERROR_NONE) {
			check(file.size == 5 &&
					memcmp(file.data, "hello", 5) == 0,
				"read exact file bytes");
			free(file.data);
		}
	}

	mh_archive_close(archive);
	mh_memory_resources_close(source);
}

static void
check_mixed_image(void)
{
	unsigned char
		bytes[BLOCKS * MH_DISC_BLOCK_BYTES + MH_DISC_SECTOR_BYTES];
	struct mh_resource_entry resource;
	const struct mh_resource_source *source;
	struct mh_archive *archive;
	struct mh_file file;
	struct mh_error error;
	enum mh_error_code status;

	memset(bytes, 0xa5, sizeof(bytes));
	make_image(bytes);
	resource.path = "mixed.bin";
	resource.data = bytes;
	resource.size = sizeof(bytes);
	source = NULL;
	archive = NULL;
	status = mh_memory_resources_open(&resource, 1, &source, &error);

	if (status == MH_ERROR_NONE)
		status = mh_iso_open_track(source, resource.path, MH_DISC_ISO,
			BLOCKS * MH_DISC_BLOCK_BYTES, &archive, &error);

	check(status == MH_ERROR_NONE,
		"ISO opens cooked data followed by raw audio");

	if (status == MH_ERROR_NONE) {
		status = mh_archive_read(archive, 0, 5, &file, &error);

		check(status == MH_ERROR_NONE, "read mixed image member");

		if (status == MH_ERROR_NONE) {
			check(memcmp(file.data, "hello", 5) == 0,
				"mixed image member retains data bytes");
			free(file.data);
		}
	}

	mh_archive_close(archive);
	archive = NULL;

	/* An otherwise plausible file extent must not reach into CD audio. */
	write_pair(bytes + ROOT_BLOCK * MH_DISC_BLOCK_BYTES + 34 + 2, BLOCKS);
	check(mh_iso_open_track(source, resource.path, MH_DISC_ISO,
		BLOCKS * MH_DISC_BLOCK_BYTES, &archive,
		&error) != MH_ERROR_NONE,
		"reject ISO member pointing into audio track");
	mh_archive_close(archive);
	mh_memory_resources_close(source);
}

int
main(void)
{
	unsigned char *bytes, *record;

	bytes = malloc(BLOCKS * MH_DISC_BLOCK_BYTES);

	if (bytes == NULL)
		return 1;

	check_mixed_image();
	make_image(bytes);
	check_image(bytes, 1);
	record = bytes + ROOT_BLOCK * MH_DISC_BLOCK_BYTES + 34;
	write_pair(record + 2, BLOCKS);
	check_image(bytes, 0);
	write_pair(record + 2, FILE_BLOCK);
	record[32] = 255;
	check_image(bytes, 0);
	make_image(bytes);
	write_record(record, FILE_BLOCK, 5, "../out;1");
	check_image(bytes, 0);
	make_image(bytes);
	record[25] = 128;
	check_image(bytes, 0);
	make_image(bytes);
	bytes[16 * MH_DISC_BLOCK_BYTES + 84] = 1;
	check_image(bytes, 0);
	free(bytes);

	return failures != 0;
}
