#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/archive/unshield_provider.h"

/* Independent Python zlib raw-Deflate fixture with a ZIP local header. */
static const unsigned char member[] = {0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x02,
	0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0xcc, 0x68, 0x11, 0xa0, 0x17,
	0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x64,
	0x61, 0x74, 0x61, 0x31, 0x2e, 0x63, 0x61, 0x62, 0xf3, 0x0c, 0x4e, 0xd6,
	0x28, 0x49, 0x2d, 0x2e, 0x51, 0x48, 0x4e, 0x4c, 0xca, 0xcc, 0x4b, 0x2d,
	0x51, 0x48, 0xaa, 0x04, 0x72, 0x35, 0x01};

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
reject(const unsigned char *bytes, size_t size)
{
	struct mh_file file;
	struct mh_error error;

	file.data = NULL;
	file.size = 17;
	check(mh_unshield_zip_cabinet(bytes, size, &file, &error) !=
				MH_ERROR_NONE &&
			file.data == NULL && file.size == 17,
		"broken ZIP member rejected without changing output");
}

int
main(void)
{
	static const unsigned int offsets[] = {6, 8, 14, 18, 22, 26, 28, 30};
	static const char expected[] = "ISc(test cabinet bytes)";
	unsigned char bytes[sizeof(member) + 4];
	struct mh_file file;
	struct mh_error error;
	size_t i;

	memcpy(bytes, member, sizeof(member));
	memset(bytes + sizeof(member), 0, 4);
	check(mh_unshield_zip_cabinet(bytes, sizeof(bytes), &file, &error) ==
			MH_ERROR_NONE,
		"decode standalone ZIP member with trailing SFX bytes");

	if (failures == 0) {
		check(file.size == sizeof(expected) - 1 &&
				memcmp(file.data, expected, file.size) == 0,
			"ZIP member bytes match independent fixture");
		free(file.data);
	}

	for (i = 0; i < sizeof(member); i++)
		reject(member, i);

	for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
		memcpy(bytes, member, sizeof(member));
		bytes[offsets[i]] ^= 1;
		reject(bytes, sizeof(bytes));
	}

	return failures != 0;
}
