#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/myo.h"
#include "engine/core/error.h"

static const char *block_names[MH_MYO_BLOCK_COUNT] = {"vertices", "primitives",
	"block_2", "block_3", "block_4", "block_5", "texture_names"};

static int
read_model_file(const char *path, unsigned char **data, size_t *data_size)
{
	FILE *stream;
	unsigned char *bytes;
	long length;
	size_t size, received;
	int close_result;

	stream = fopen(path, "rb");

	if (stream == NULL) {
		fprintf(stderr, "%s: cannot open file\n", path);
		return 1;
	}

	if (fseek(stream, 0, SEEK_END) != 0 || (length = ftell(stream)) < 0 ||
		fseek(stream, 0, SEEK_SET) != 0) {
		fprintf(stderr, "%s: cannot determine file size\n", path);
		fclose(stream);
		return 1;
	}

	if ((unsigned long)length > (size_t)-1) {
		fprintf(stderr, "%s: file is too large\n", path);
		fclose(stream);
		return 1;
	}

	size = (size_t)length;
	bytes = (unsigned char *)malloc(size == 0 ? 1 : size);

	if (bytes == NULL) {
		fprintf(stderr, "%s: cannot allocate input buffer\n", path);
		fclose(stream);
		return 1;
	}

	received = fread(bytes, 1, size, stream);
	close_result = fclose(stream);

	if (received != size || close_result != 0) {
		fprintf(stderr, "%s: cannot read file\n", path);
		free(bytes);
		return 1;
	}

	*data = bytes;
	*data_size = size;

	return 0;
}

static void
print_model(const char *path, const struct mh_myo *model, size_t size)
{
	const char *name;
	int i;
	unsigned long index;

	printf("%s: MYO v%d, %ld bytes\n", path, model->version, (long)size);

	for (i = 0; i < MH_MYO_BLOCK_COUNT; i++) {
		printf("  %-14s count=%lu bytes=%lu offset=0x%lx\n",
			block_names[i], model->blocks[i].count,
			(unsigned long)model->blocks[i].size,
			(unsigned long)model->blocks[i].offset);
	}

	printf("  bounds: (%.9g, %.9g, %.9g) to (%.9g, %.9g, %.9g)\n",
		model->minimum[0], model->minimum[1], model->minimum[2],
		model->maximum[0], model->maximum[1], model->maximum[2]);
	printf("  primitive types:");

	for (i = 0; i < MH_MYO_PRIMITIVE_TYPES; i++) {
		if (model->primitive_types[i] != 0) {
			printf(" %d=%lu", i, model->primitive_types[i]);
		}
	}

	printf("\n  auxiliary records: %lu\n", model->auxiliary_count);
	name = (const char *)model->data +
		model->blocks[MH_MYO_TEXTURE_NAMES].offset;

	for (index = 0; index < model->blocks[MH_MYO_TEXTURE_NAMES].count;
		index++) {
		printf("  texture: %s\n", name);
		name += strlen(name) + 1;
	}
}

static int
inspect_model(const char *path, int quiet)
{
	unsigned char *bytes;
	size_t size;
	struct mh_myo model;
	struct mh_error error;
	enum mh_error_code status;

	if (read_model_file(path, &bytes, &size) != 0)
		return 1;

	status = mh_myo_parse(bytes, size, &model, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s: %s\n", path, error.message);
		free(bytes);
		return 1;
	}

	if (!quiet)
		print_model(path, &model, size);

	free(bytes);

	return 0;
}

int
main(int argc, char *argv[])
{
	int i;
	int result;
	int quiet;
	int first;

	quiet = argc > 1 && strcmp(argv[1], "--quiet") == 0;
	first = quiet ? 2 : 1;

	if (argc <= first) {
		fprintf(stderr,
			"Usage: mh-myo-info [--quiet] FILE.MYO [FILE.MYO ...]\n");
		return 2;
	}

	result = 0;

	for (i = first; i < argc; i++) {
		if (inspect_model(argv[i], quiet) != 0) {
			result = 1;
		}
	}

	return result;
}
