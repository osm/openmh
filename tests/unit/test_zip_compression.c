#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "backends/storage/zip_provider.h"
#include "engine/archive/zip_write.h"
#include "engine/core/binary.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "compressed writer line %d: %s\n",     \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
make_package(const unsigned char *bytes, size_t size,
	enum mh_zip_compression compression, struct mh_file *output)
{
	struct mh_resource_entry entry;
	const struct mh_resource_source *source;
	const struct mh_user_data *data;
	struct mh_data_directory *directory;
	struct mh_data_stream *stream;
	struct mh_data_file *token;
	struct mh_zip_writer *writer;

	entry.path = "source";
	entry.data = bytes;
	entry.size = size;
	CHECK(mh_memory_resources_open(&entry, 1, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(data, "out", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "out", "source", &directory,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_open(directory, &stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_zip_writer_open(stream, &writer, NULL) == MH_ERROR_NONE);
	CHECK(mh_zip_writer_add(writer, source, "source", "asset", 0,
		compression, NULL) == MH_ERROR_NONE);
	CHECK(mh_zip_writer_finish(writer, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_seal(stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_stream_create(stream, "result.ass", NULL, NULL, &token,
		NULL) == MH_ERROR_NONE);
	mh_data_file_close(token);
	mh_zip_writer_close(writer);
	mh_data_stream_close(stream);
	CHECK(mh_data_directory_read(directory, "result.ass", size + 1024,
		output, &token, NULL) == MH_ERROR_NONE);
	mh_data_file_close(token);
	mh_data_directory_close(directory);
	mh_memory_user_data_close(data);
	mh_memory_resources_close(source);

	return 0;
}

static int
check_case(const unsigned char *bytes, size_t size,
	enum mh_zip_compression compression, unsigned int method)
{
	struct mh_file package, decoded;
	struct mh_resource_entry entry;
	const struct mh_resource_source *memory, *zip;

	CHECK(make_package(bytes, size, compression, &package) == 0);
	CHECK(mh_read_u16le(package.data + 8) == method);
	CHECK(mh_read_u32le(package.data + 22) == size);
	CHECK(method == 0 || mh_read_u32le(package.data + 18) < size);
	entry.path = "game.ass";
	entry.data = package.data;
	entry.size = package.size;
	CHECK(mh_memory_resources_open(&entry, 1, &memory, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_zip_resource_provider()->open(
		memory, entry.path, &zip, NULL) == MH_ERROR_NONE);
	CHECK(mh_resource_source_read(zip, "game.ass/asset", &decoded, NULL) ==
		MH_ERROR_NONE);
	CHECK(decoded.size == size && memcmp(decoded.data, bytes, size) == 0);
	free(decoded.data);
	mh_zip_resource_provider()->close(zip);
	mh_memory_resources_close(memory);
	free(package.data);

	return 0;
}

int
main(void)
{
	unsigned char *bytes;
	unsigned long random;
	size_t i, size;

	size = 150001;
	bytes = malloc(size);
	CHECK(bytes != NULL);
	memset(bytes, 'x', size);
	CHECK(check_case(bytes, size, MH_ZIP_COMPRESS, 8) == 0);
	CHECK(check_case(bytes, size, MH_ZIP_STORE, 0) == 0);
	CHECK(check_case(bytes, 0, MH_ZIP_COMPRESS, 0) == 0);
	CHECK(check_case(bytes, 1, MH_ZIP_COMPRESS, 0) == 0);

	/*
	 * Incompressible bytes exercise fallback after multiple input chunks.
	 */
	random = 127;

	for (i = 0; i < size; i++) {
		random ^= (random << 13) & 0xffffffffUL;
		random ^= random >> 17;
		random ^= (random << 5) & 0xffffffffUL;
		bytes[i] = (unsigned char)random;
	}

	CHECK(check_case(bytes, size, MH_ZIP_COMPRESS, 0) == 0);
	free(bytes);
	puts("ZIP compression, storage policy and incompressible fallback pass");

	return 0;
}
