#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "backends/storage/zip_provider.h"
#include "engine/archive/zip_write.h"
#include "engine/core/binary.h"
#include "engine/storage/asset_path.h"
#include "engine/storage/vfs.h"
#include "interfaces/storage/data_directory.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "package line %d: %s\n", __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

/* Independent fixture produced by Python's zipfile, not our writer. */
static const unsigned char stored_zip[] = {0x50, 0x4b, 0x03, 0x04, 0x14, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x00, 0x78, 0x7b, 0xac, 0x5a,
	0x0a, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00,
	0x44, 0x61, 0x74, 0x61, 0x2f, 0x74, 0x65, 0x73, 0x74, 0x2e, 0x74, 0x78,
	0x74, 0x6d, 0x6f, 0x74, 0x6f, 0x72, 0x68, 0x65, 0x61, 0x64, 0x0a, 0x50,
	0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x06, 0x00, 0x00, 0x00, 0x45, 0x6d, 0x70, 0x74, 0x79, 0x2f, 0x50,
	0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x21, 0x00, 0x78, 0x7b, 0xac, 0x5a, 0x0a, 0x00, 0x00, 0x00, 0x0a,
	0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x44, 0x61, 0x74,
	0x61, 0x2f, 0x74, 0x65, 0x73, 0x74, 0x2e, 0x74, 0x78, 0x74, 0x50, 0x4b,
	0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x01, 0x35, 0x00, 0x00, 0x00, 0x45, 0x6d, 0x70, 0x74,
	0x79, 0x2f, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x02, 0x00, 0x6f, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x00, 0x00};

struct package_mutation {
	size_t		offset;
	unsigned char	value;
};

static enum mh_directory_action
count_name(void *context, const char *name)
{
	unsigned int *count;

	(void)name;
	count = context;
	(*count)++;

	return MH_DIRECTORY_CONTINUE;
}

static int
check_reads(const struct mh_resource_source *source)
{
	const struct mh_resource_archive_provider *provider;
	const struct mh_resource_source *zip;
	struct mh_file file;
	struct mh_data_directory *directory;
	struct mh_data_file *token;
	struct mh_error error;
	char path[128], bytes[4];
	unsigned long size;
	unsigned int count;
	int removable;

	provider = mh_zip_resource_provider();
	CHECK(provider->open(source, "game/openmotor.ass", &zip, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_asset_path_from(zip, "game/openmotor.ass", "data/TEST.TXT",
		path, sizeof(path), &error) == MH_ERROR_NONE);
	CHECK(mh_resource_source_size(zip, path, &size, NULL) ==
			MH_ERROR_NONE &&
		size == 10);
	CHECK(mh_resource_source_read_at(zip, path, 2, bytes, 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(bytes, "torh", 4) == 0);
	CHECK(mh_resource_source_read_at(zip, path, 9, bytes, 4, NULL) ==
		MH_ERROR_RANGE);
	CHECK(mh_resource_source_read_bounded(zip, path, 9, &file, NULL) ==
		MH_ERROR_RANGE);
	CHECK(mh_resource_source_read(zip, path, &file, NULL) == MH_ERROR_NONE);
	CHECK(file.size == 10 && memcmp(file.data, "motorhead\n", 10) == 0);
	free(file.data);
	CHECK(mh_resource_source_open_directory(zip, "game/openmotor.ass/Data",
		&directory, NULL) == MH_ERROR_NONE);
	count = 0;
	CHECK(mh_data_directory_list(directory, count_name, &count, NULL) ==
			MH_ERROR_NONE &&
		count == 1);
	CHECK(mh_data_directory_read(directory, "test.txt", 10, &file, &token,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_validate(token, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_remove(token, NULL) == MH_ERROR_UNSUPPORTED);
	free(file.data);
	mh_data_file_close(token);
	CHECK(mh_data_directory_inspect(directory, "test.txt", &token,
		&removable, NULL) == MH_ERROR_NONE &&
		!removable);
	mh_data_file_close(token);
	mh_data_directory_close(directory);
	CHECK(mh_resource_source_open_directory(zip, "game/openmotor.ass/Empty",
		&directory, NULL) == MH_ERROR_NONE);
	count = 0;
	CHECK(mh_data_directory_list(directory, count_name, &count, NULL) ==
			MH_ERROR_NONE &&
		count == 0);
	mh_data_directory_close(directory);
	CHECK(mh_resource_source_read(zip, "outside/test.txt", &file, NULL) ==
		MH_ERROR_NOT_FOUND);
	provider->close(zip);

	return 0;
}

static int
check_mounts(const struct mh_resource_source *source)
{
	struct mh_vfs *vfs, *duplicate[2], *combined;

	CHECK(mh_vfs_open(source, mh_zip_resource_provider(), "game", &vfs,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(mh_vfs_root(vfs), "game") == 0);
	CHECK(mh_vfs_resources(vfs) == source);
	CHECK(strcmp(mh_vfs_installation(vfs), "game") == 0);
	duplicate[0] = duplicate[1] = vfs;
	combined = NULL;
	CHECK(mh_vfs_combine(duplicate, NULL, 2, &combined, NULL) ==
		MH_ERROR_INVALID);
	CHECK(combined == NULL);
	mh_vfs_close(vfs);
	CHECK(mh_vfs_open(source, mh_zip_resource_provider(),
		"game/openmotor.ass", &vfs, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(mh_vfs_installation(vfs), "game/") == 0);
	mh_vfs_close(vfs);
	CHECK(mh_vfs_open(source, mh_zip_resource_provider(), "loose", &vfs,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_vfs_resources(vfs) == source);
	CHECK(strcmp(mh_vfs_root(vfs), "loose") == 0);
	mh_vfs_close(vfs);

	return 0;
}

static int
check_rejections(void)
{
	static const struct package_mutation mutations[] = {
		{0, 0},				    /* local signature */
		{8, 8},				    /* local compression */
		{30, '/'},			    /* absolute name */
		{30, 0},			    /* embedded NUL */
		{99, 8},			    /* central compression */
		{97, 1},			    /* encrypted */
		{131, 255},			    /* invalid local offset */
		{sizeof(stored_zip) - 22 + 4, 1},   /* split archive */
		{sizeof(stored_zip) - 22 + 16, 255} /* invalid central offset */
	};

	unsigned char bytes[sizeof(stored_zip)];
	struct mh_resource_entry entry;
	const struct mh_resource_source *source, *zip;
	size_t i;

	entry.path = "broken.ass";
	entry.data = bytes;
	entry.size = sizeof(bytes);
	CHECK(mh_memory_resources_open(&entry, 1, &source, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < sizeof(mutations) / sizeof(mutations[0]); i++) {
		memcpy(bytes, stored_zip, sizeof(bytes));
		bytes[mutations[i].offset] = mutations[i].value;
		zip = NULL;
		CHECK(mh_zip_resource_provider()->open(
			source, entry.path, &zip, NULL) != MH_ERROR_NONE);
		CHECK(zip == NULL);
	}

	/* Both headers agree, but the payload extends into the next header. */
	memcpy(bytes, stored_zip, sizeof(bytes));
	mh_write_u32le(bytes + 18, 11);
	mh_write_u32le(bytes + 22, 11);
	mh_write_u32le(bytes + 89 + 20, 11);
	mh_write_u32le(bytes + 89 + 24, 11);
	CHECK(mh_zip_resource_provider()->open(
		source, entry.path, &zip, NULL) == MH_ERROR_INVALID);

	/*
	 * Entry integrity is checked on whole-resource reads, not metadata
	 * mount.
	 */
	memcpy(bytes, stored_zip, sizeof(bytes));
	bytes[43] ^= 1;
	CHECK(mh_zip_resource_provider()->open(
		source, entry.path, &zip, NULL) == MH_ERROR_NONE);
	{
		struct mh_file file;

		CHECK(mh_resource_source_read(zip, "broken.ass/Data/test.txt",
			&file, NULL) == MH_ERROR_INVALID);
	}

	mh_zip_resource_provider()->close(zip);
	mh_memory_resources_close(source);

	return 0;
}

static int
check_truncated_index(void)
{
	struct mh_resource_entry entry;
	const struct mh_resource_source *source, *zip;
	size_t size;

	entry.path = "truncated.ass";
	entry.data = stored_zip;

	for (size = 0; size < sizeof(stored_zip); size++) {
		entry.size = size;
		CHECK(mh_memory_resources_open(&entry, 1, &source, NULL) ==
			MH_ERROR_NONE);
		zip = NULL;
		CHECK(mh_zip_resource_provider()->open(source, entry.path, &zip,
			NULL) != MH_ERROR_NONE &&
			zip == NULL);
		mh_memory_resources_close(source);
	}

	return 0;
}

static int
check_writer(const struct mh_resource_source *source)
{
	const struct mh_user_data *data;
	struct mh_data_directory *directory;
	struct mh_data_stream *stream;
	struct mh_data_file *token;
	struct mh_zip_writer *writer;
	struct mh_file file;
	struct mh_resource_entry entry;
	const struct mh_resource_source *packed;

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(data, "out", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(
		data, "out", "loose", &directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_open(directory, &stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_zip_writer_open(stream, &writer, NULL) == MH_ERROR_NONE);
	CHECK(mh_zip_writer_add(writer, source, "loose/test.txt",
		"Data/test.txt", 0, MH_ZIP_STORE, NULL) == MH_ERROR_NONE);
	CHECK(mh_zip_writer_add(writer, NULL, NULL, "Empty", 1, MH_ZIP_STORE,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_zip_writer_add(writer, source, "loose/test.txt",
		"data/TEST.txt", 0, MH_ZIP_STORE,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_zip_writer_add(writer, NULL, NULL, "Data", 1, MH_ZIP_STORE,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_zip_writer_add(writer, source, "loose/test.txt", "Data-alpha",
		0, MH_ZIP_STORE, NULL) == MH_ERROR_NONE);
	CHECK(mh_zip_writer_finish(writer, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_seal(stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_create(stream, "game.ass", NULL, NULL, &token,
		NULL) == MH_ERROR_NONE);
	mh_data_file_close(token);
	mh_zip_writer_close(writer);
	mh_data_stream_close(stream);
	CHECK(mh_data_directory_read(directory, "game.ass", 4096, &file, &token,
		NULL) == MH_ERROR_NONE);
	CHECK(file.size == sizeof(stored_zip) + 192);
	CHECK(mh_read_u32le(file.data) == 0x04034b50UL);
	CHECK(memcmp(file.data + 43, "motorhead\n", 10) == 0);
	entry.path = "game/openmotor.ass";
	entry.data = file.data;
	entry.size = file.size;
	CHECK(mh_memory_resources_open(&entry, 1, &packed, NULL) ==
		MH_ERROR_NONE);
	CHECK(check_reads(packed) == 0);
	{
		const struct mh_resource_source *zip;
		unsigned int count;

		CHECK(mh_zip_resource_provider()->open(
			packed, entry.path, &zip, NULL) == MH_ERROR_NONE);
		count = 0;
		CHECK(mh_resource_source_list(zip, entry.path, count_name,
			&count, NULL) == MH_ERROR_NONE &&
			count == 3);
		mh_zip_resource_provider()->close(zip);
	}

	mh_memory_resources_close(packed);
	free(file.data);
	mh_data_file_close(token);
	mh_data_directory_close(directory);
	mh_memory_user_data_close(data);

	return 0;
}

int
main(void)
{
	struct mh_resource_entry entries[2];
	const struct mh_resource_source *source;

	entries[0].path = "game/openmotor.ass";
	entries[0].data = stored_zip;
	entries[0].size = sizeof(stored_zip);
	entries[1].path = "loose/test.txt";
	entries[1].data = (const unsigned char *)"motorhead\n";
	entries[1].size = 10;
	CHECK(mh_memory_resources_open(entries, 2, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(check_reads(source) == 0);
	CHECK(check_mounts(source) == 0);
	CHECK(check_rejections() == 0);
	CHECK(check_truncated_index() == 0);
	CHECK(check_writer(source) == 0);
	mh_memory_resources_close(source);
	puts("stored ZIP resources, catalogs, VFS selection and writer pass");

	return 0;
}
