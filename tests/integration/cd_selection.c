#include <stdio.h>
#include <string.h>

#include "engine/audio/cd.h"
#include "engine/core/error.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "CD selection line %d\n", __LINE__);   \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_cd_request request, saved;
	unsigned int mode;

	CHECK(mh_cd_select(MH_CD_MENU, 255, 0, 3, 3, &request, NULL) ==
		MH_ERROR_NONE);
	CHECK(request.scan && request.stop && request.volume && request.play &&
		request.track == 3);
	CHECK(mh_cd_select(MH_CD_RESTORE_MENU, 1, 0, 259, 3, &request, NULL) ==
			MH_ERROR_NONE &&
		request.play && request.track == 3);
	CHECK(mh_cd_select(MH_CD_RESTORE_MENU, 1, 0, 259, 259, &request,
		NULL) == MH_ERROR_NONE &&
		!request.play);

	for (mode = 0; mode < 7; mode++) {
		CHECK(mh_cd_select(MH_CD_RACE, 255, mode, 260, 0, &request,
			NULL) == MH_ERROR_NONE);
		CHECK(request.play == (mode != 4) &&
			request.stop == request.play &&
			request.track == (mode == 4 ? 0U : 4U));
		CHECK(mh_cd_select(MH_CD_RESULTS, 255, mode, 11, 0, &request,
			NULL) == MH_ERROR_NONE);
		CHECK(request.play == (mode < 2) && !request.stop &&
			request.track == (mode < 2 ? 2U : 0U));
		CHECK(mh_cd_select(MH_CD_MENU, 0, mode, 3, 4, &request, NULL) ==
				MH_ERROR_NONE &&
			!request.play && !request.stop);
	}

	saved = request;
	CHECK(mh_cd_select(MH_CD_RACE, 256, 0, 4, 0, &request, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&request, &saved, sizeof(saved)) == 0);
	CHECK(mh_cd_select(MH_CD_RACE, 255, 7, 4, 0, &request, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&request, &saved, sizeof(saved)) == 0);
	CHECK(mh_cd_select((enum mh_cd_boundary)99, 255, 0, 4, 0, &request,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_cd_select(MH_CD_RACE, 255, 0, 4, 0, NULL, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_cd_repeat(255, 1) && !mh_cd_repeat(0, 1) &&
		!mh_cd_repeat(255, 0));
	CHECK(mh_cd_repeat_select(255, 1, 259, 1000, 5000, 6000, &request,
		NULL) == MH_ERROR_NONE &&
		!request.play);
	CHECK(mh_cd_repeat_select(255, 1, 259, 1000, 5000, 6000.1L, &request,
		NULL) == MH_ERROR_NONE &&
		request.play && request.track == 3);
	CHECK(mh_cd_repeat_select(255, 1, 256, 1000, 5000, 6001, &request,
		NULL) == MH_ERROR_NONE &&
		request.play && request.track == 0);
	CHECK(mh_cd_repeat_select(255, 1, 3, 0xfffffff0UL, 100, 85, &request,
		NULL) == MH_ERROR_NONE &&
		request.play);
	CHECK(mh_cd_repeat_select(0, 1, 3, 0, 0, 5000, &request, NULL) ==
			MH_ERROR_NONE &&
		!request.play);
	CHECK(mh_cd_repeat_select(255, 0, 3, 0, 0, 5000, &request, NULL) ==
			MH_ERROR_NONE &&
		!request.play);
	CHECK(mh_cd_repeat_select(255, 1, 0, 0, 0, 5000, &request, NULL) ==
			MH_ERROR_NONE &&
		!request.play);
	saved = request;
	CHECK(mh_cd_repeat_select(255, 1, 3, 0, 0, -1, &request, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&request, &saved, sizeof(saved)) == 0);
	CHECK(mh_cd_repeat_select(255, 1, 3, 0, 0, 9223372036854775808.0L,
		&request, NULL) != MH_ERROR_NONE);
	CHECK(mh_cd_repeat_select(256, 1, 3, 0, 0, 0, &request, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_cd_repeat_select(255, 2, 3, 0, 0, 0, &request, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_cd_repeat_select(255, 1, 3, 0, 0, 0, NULL, NULL) !=
		MH_ERROR_NONE);
	puts("CD selection gates, byte narrowing, results, menu restore and repeat passed");

	return 0;
}
