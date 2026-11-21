#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resource_pack.h"

#define PACK_ENTRY_LIMIT 4096
#define PACK_BYTE_LIMIT (256UL * 1024 * 1024)
#define PACK_PATH_SIZE 1024

static int
read_number(FILE *file, unsigned long limit, unsigned long *output)
{
	char line[32], *end;
	unsigned long value;

	if (fgets(line, sizeof(line), file) == NULL || line[0] < '0' ||
		line[0] > '9')
		return 0;

	value = strtoul(line, &end, 10);

	if (value > limit || strcmp(end, "\n") != 0)
		return 0;

	*output = value;

	return 1;
}

static int
read_entry(FILE *file, struct mh_resource_entry *entry, unsigned long *total)
{
	char line[PACK_PATH_SIZE], *path;
	unsigned char *bytes;
	unsigned long size;
	size_t length;

	if (fgets(line, sizeof(line), file) == NULL)
		return 0;

	length = strlen(line);

	if (length < 2 || line[length - 1] != '\n' ||
		!read_number(file, PACK_BYTE_LIMIT - *total, &size))
		return 0;

	line[length - 1] = '\0';
	path = malloc(length);
	bytes = malloc(size == 0 ? 1 : (size_t)size);

	if (path == NULL || bytes == NULL ||
		fread(bytes, 1, (size_t)size, file) != (size_t)size) {
		free(path);
		free(bytes);
		return 0;
	}

	memcpy(path, line, length);
	entry->path = path;
	entry->data = bytes;
	entry->size = (size_t)size;
	*total += size;

	return 1;
}

void
mh_test_resource_pack_free(struct mh_test_resource_pack *pack)
{
	size_t i;

	for (i = 0; i < pack->count; i++) {
		free((void *)pack->entries[i].path);
		free((void *)pack->entries[i].data);
	}

	free(pack->entries);
	memset(pack, 0, sizeof(*pack));
}

int
mh_test_resource_pack_load(struct mh_test_resource_pack *pack, const char *path)
{
	FILE *file;
	unsigned long count, total;
	int success;

	memset(pack, 0, sizeof(*pack));
	file = fopen(path, "rb");

	if (file == NULL)
		return 0;

	success = read_number(file, PACK_ENTRY_LIMIT, &count) && count != 0;

	if (success) {
		pack->entries = calloc((size_t)count, sizeof(*pack->entries));
		success = pack->entries != NULL;
	}

	total = 0;

	while (success && pack->count < count) {
		success = read_entry(file, &pack->entries[pack->count], &total);

		if (success)
			pack->count++;
	}

	if (success)
		success = fgetc(file) == EOF && !ferror(file);

	if (fclose(file) != 0)
		success = 0;

	if (!success)
		mh_test_resource_pack_free(pack);

	return success;
}
