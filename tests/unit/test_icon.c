#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "engine/assets/executable_icon.h"
#include "engine/assets/ico.h"
#include "engine/assets/icon.h"
#include "engine/core/binary.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "icon:%d: %s\n", __LINE__, #x);        \
			return 1;                                              \
		}                                                              \
	} while (0)

enum {
	DIB_BYTES = 64,
	ICO_BYTES = 22 + DIB_BYTES,
	PE_RESOURCE_OFFSET = 512,
	PE_ICON_OFFSET = PE_RESOURCE_OFFSET + 88,
	PE_BYTES = PE_ICON_OFFSET + DIB_BYTES
};

static void
make_ico(unsigned char *ico, unsigned int depth)
{
	unsigned char *dib;

	memset(ico, 0, ICO_BYTES);
	ico[2] = 1;
	ico[4] = 1;
	ico[6] = ico[7] = 2;
	mh_write_u32le(ico + 14, DIB_BYTES);
	mh_write_u32le(ico + 18, 22);
	dib = ico + 22;
	mh_write_u32le(dib, 40);
	mh_write_u32le(dib + 4, 2);
	mh_write_u32le(dib + 8, 4);
	dib[12] = 1;
	dib[14] = (unsigned char)depth;
	mh_write_u32le(dib + 32, 2);

	/*
	 * Palette entries: red, green. Each pixel and mask row is padded to 4.
	 */
	dib[42] = 255;
	dib[45] = 255;
	dib[48] = depth == 4 ? 0x10 : 1;
	dib[53] = depth == 8 ? 1 : 0;
	dib[52] = depth == 4 ? 0x01 : 0;
	dib[60] = 0x40;
}

static void
make_executable(unsigned char *exe, const unsigned char *dib)
{
	unsigned char *optional, *section, *root;

	memset(exe, 0, PE_BYTES);
	memcpy(exe, "MZ", 2);
	mh_write_u32le(exe + 60, 64);
	memcpy(exe + 64, "PE\0\0", 4);
	exe[70] = 2;
	exe[84] = 224;
	optional = exe + 88;
	optional[0] = 0x0b;
	optional[1] = 1;
	mh_write_u32le(optional + 92, 16);
	mh_write_u32le(optional + 112, 0x1000);
	mh_write_u32le(optional + 116, PE_BYTES - PE_RESOURCE_OFFSET);
	section = optional + 224;

	/* The resource RVA is exactly at the preceding section's end. */
	mh_write_u32le(section + 12, 0x0f00);
	mh_write_u32le(section + 16, 256);
	mh_write_u32le(section + 20, 256);
	section += 40;
	mh_write_u32le(section + 12, 0x1000);
	mh_write_u32le(section + 16, PE_BYTES - PE_RESOURCE_OFFSET);
	mh_write_u32le(section + 20, PE_RESOURCE_OFFSET);
	root = exe + PE_RESOURCE_OFFSET;

	/* RT_ICON -> resource 1 -> language 0 -> DIB. */
	root[14] = root[38] = root[62] = 1;
	mh_write_u32le(root + 16, 3);
	mh_write_u32le(root + 20, 0x80000018UL);
	mh_write_u32le(root + 40, 1);
	mh_write_u32le(root + 44, 0x80000030UL);
	mh_write_u32le(root + 68, 72);
	mh_write_u32le(root + 72, 0x1058);
	mh_write_u32le(root + 76, DIB_BYTES);
	memcpy(exe + PE_ICON_OFFSET, dib, DIB_BYTES);
}

static int
check_pixels(struct mh_image *image)
{
	static const unsigned char expected[] = {
		255, 0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 255, 255, 0, 0, 255};

	CHECK(image->width == 2 && image->height == 2);
	CHECK(memcmp(image->rgba, expected, sizeof(expected)) == 0);
	free(image->rgba);
	image->rgba = NULL;

	return 0;
}

static int
check_sources(const unsigned char *ico, const unsigned char *exe)
{
	struct mh_resource_entry entries[3];
	const struct mh_resource_source *resources;
	struct mh_image image;
	unsigned int i;

	entries[0].path = "game/motor.ico";
	entries[1].path = "game/Original/disc/MOTOR.ICO";
	entries[2].path = "game/MOTOR.EXE";

	for (i = 0; i < 3; i++) {
		entries[i].data = i == 2 ? exe : ico;
		entries[i].size = i == 2 ? PE_BYTES : ICO_BYTES;
	}

	for (i = 0; i < 3; i++) {
		CHECK(mh_memory_resources_open(entries + i, 3 - i, &resources,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_icon_load_from(resources, "game", &image, NULL) ==
			MH_ERROR_NONE);
		CHECK(check_pixels(&image) == 0);
		mh_memory_resources_close(resources);
	}

	/* A damaged separate icon can still use the executable fallback. */
	entries[0].size = entries[1].size = 1;
	CHECK(mh_memory_resources_open(entries, 3, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_icon_load_from(resources, "game", &image, NULL) ==
		MH_ERROR_NONE);
	CHECK(check_pixels(&image) == 0);
	mh_memory_resources_close(resources);

	return 0;
}

int
main(void)
{
	unsigned char ico[ICO_BYTES], exe[PE_BYTES];
	struct mh_image image;
	size_t size;
	unsigned int depth;

	for (depth = 4; depth <= 8; depth += 4) {
		make_ico(ico, depth);
		CHECK(mh_image_ico_decode(ico, sizeof(ico), &image, NULL) ==
			MH_ERROR_NONE);
		CHECK(check_pixels(&image) == 0);
	}

	image.width = 17;
	image.height = 19;
	image.rgba = NULL;

	for (size = 0; size < sizeof(ico); size++) {
		CHECK(mh_image_ico_decode(ico, size, &image, NULL) !=
			MH_ERROR_NONE);
		CHECK(image.width == 17 && image.height == 19 &&
			image.rgba == NULL);
	}

	make_executable(exe, ico + 22);
	CHECK(mh_executable_icon_decode(exe, sizeof(exe), &image, NULL) ==
		MH_ERROR_NONE);
	CHECK(check_pixels(&image) == 0);

	for (size = 0; size < sizeof(exe); size++)
		CHECK(mh_executable_icon_decode(exe, size, &image, NULL) !=
			MH_ERROR_NONE);

	CHECK(check_sources(ico, exe) == 0);

	/* Reject an icon payload outside its section and a cyclic directory. */
	mh_write_u32le(exe + PE_RESOURCE_OFFSET + 72, 0xffffffffUL);
	CHECK(mh_executable_icon_decode(exe, sizeof(exe), &image, NULL) ==
		MH_ERROR_INVALID);
	make_executable(exe, ico + 22);
	mh_write_u32le(exe + PE_RESOURCE_OFFSET + 68, 0x80000000UL);
	CHECK(mh_executable_icon_decode(exe, sizeof(exe), &image, NULL) ==
		MH_ERROR_INVALID);
	ico[22 + 48] = 2;
	CHECK(mh_image_ico_decode(ico, sizeof(ico), &image, NULL) ==
		MH_ERROR_INVALID);

	puts("Icons: indexed pixels, mask, bounds, PE resources and installation fallback passed");

	return 0;
}
