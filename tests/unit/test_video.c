#include <stdio.h>
#include <string.h>

#include "engine/assets/divi.h"
#include "engine/core/error.h"
#include "engine/game/league.h"
#include "engine/video/layout.h"

#define VIDEO_HEADER_BYTES 780

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
put_u32le(unsigned char *bytes, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++) {
		bytes[i] = (unsigned char)(value & 255);
		value >>= 8;
	}
}

int
main(void)
{
	unsigned char bytes[VIDEO_HEADER_BYTES];
	struct mh_video_divi_index index, before;
	unsigned int i, video, x, y;

	CHECK(mh_video_origin(640, 240, 640, 480, &x, &y, NULL) ==
			MH_ERROR_NONE &&
		x == 0 && y == 120);
	CHECK(mh_video_origin(640, 400, 640, 480, &x, &y, NULL) ==
			MH_ERROR_NONE &&
		x == 0 && y == 40);
	CHECK(mh_video_origin(319, 239, 640, 480, &x, &y, NULL) ==
			MH_ERROR_NONE &&
		x == 160 && y == 120);
	CHECK(mh_video_origin(641, 240, 640, 480, &x, &y, NULL) !=
			MH_ERROR_NONE &&
		x == 160 && y == 120);
	CHECK(mh_video_origin(640, 0, 640, 480, &x, &y, NULL) !=
			MH_ERROR_NONE &&
		x == 160 && y == 120);
	video = 99;
	CHECK(mh_league_video(0, &video, NULL) != MH_ERROR_NONE && video == 99);
	CHECK(mh_league_video(8, &video, NULL) != MH_ERROR_NONE && video == 99);
	CHECK(mh_league_video(1, NULL, NULL) != MH_ERROR_NONE);
	memset(bytes, 0, sizeof(bytes));
	memcpy(bytes, "DIVI", 4);
	put_u32le(bytes + 4, 2);
	put_u32le(bytes + 8, VIDEO_HEADER_BYTES);
	put_u32le(bytes + 12, 1);
	put_u32le(bytes + 16, VIDEO_HEADER_BYTES);
	put_u32le(bytes + 20, 0xabcdefUL);
	put_u32le(bytes + 28, 800);
	CHECK(mh_video_divi_index(bytes, sizeof(bytes), 1000, &index, NULL) ==
		MH_ERROR_NONE);
	CHECK(index.count == 2 && index.entries[0].size == 20 &&
		index.entries[1].size == 200);
	CHECK(index.entries[0].flags == 0xabcdefUL &&
		index.entries[0].kind == 1);
	before = index;

	for (i = 0; i < VIDEO_HEADER_BYTES; i++) {
		CHECK(mh_video_divi_index(bytes, i, 1000, &index, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&before, &index, sizeof(index)) == 0);
	}

	put_u32le(bytes + 28, 779);
	CHECK(mh_video_divi_index(bytes, VIDEO_HEADER_BYTES, 1000, &index,
		NULL) != MH_ERROR_NONE);
	put_u32le(bytes + 28, VIDEO_HEADER_BYTES);
	CHECK(mh_video_divi_index(bytes, VIDEO_HEADER_BYTES, 1000, &index,
		NULL) != MH_ERROR_NONE);
	put_u32le(bytes + 28, 1001);
	CHECK(mh_video_divi_index(bytes, VIDEO_HEADER_BYTES, 1000, &index,
		NULL) != MH_ERROR_NONE);
	put_u32le(bytes + 28, 800);
	put_u32le(bytes + 4, 65);
	CHECK(mh_video_divi_index(bytes, VIDEO_HEADER_BYTES, 1000, &index,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&before, &index, sizeof(index)) == 0);

	return failures != 0;
}
