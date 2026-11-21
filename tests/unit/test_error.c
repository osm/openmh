#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "error test line %d\n", __LINE__);     \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_error error, copy;
	enum mh_error_code status;
	char source[32], saved[8];

	CHECK(mh_success.code == MH_ERROR_NONE && mh_success.message == NULL);
	status = mh_error_set(NULL, MH_ERROR_IO, "status only");
	CHECK(status == MH_ERROR_IO);
	CHECK(mh_error_set(NULL, MH_ERROR_NONE, NULL) == MH_ERROR_NONE);
	status = mh_error_set(&error, MH_ERROR_PERMISSION, "preserved detail");
	CHECK(status == MH_ERROR_PERMISSION && error.code == status);
	CHECK(mh_error_set(NULL, MH_ERROR_IO, "unrelated failure") ==
		MH_ERROR_IO);
	CHECK(error.code == MH_ERROR_PERMISSION &&
		strcmp(error.message, "preserved detail") == 0);
	CHECK(mh_error_forward(NULL, &error) == MH_ERROR_PERMISSION);
	status = mh_error_forward(&copy, &error);
	CHECK(status == MH_ERROR_PERMISSION && copy.code == status &&
		copy.message == error.message);
	CHECK(mh_error_forward(NULL, NULL) == MH_ERROR_INVALID);
	CHECK(mh_error_copy(NULL, NULL, saved, sizeof(saved)) ==
		MH_ERROR_INVALID);
	status = mh_error_copy(NULL, &error, saved, sizeof(saved));
	CHECK(status == MH_ERROR_PERMISSION && strcmp(saved, "preserv") == 0);

	mh_error_set(&error, MH_ERROR_NONE, "unused diagnostic");
	CHECK(error.code == MH_ERROR_NONE && error.message == NULL);
	mh_error_set(&error, MH_ERROR_PERMISSION, "first diagnostic");
	mh_error_set(&copy, MH_ERROR_PERMISSION, "different wording");
	CHECK(error.code == copy.code);
	CHECK(strcmp(error.message, copy.message) != 0);

	strcpy(source, "original failure");
	mh_error_set(&error, MH_ERROR_IO, source);
	mh_error_copy(&copy, &error, saved, sizeof(saved));
	CHECK(copy.code == MH_ERROR_IO && copy.message == saved);
	CHECK(strcmp(saved, "origina") == 0);
	strcpy(source, "source reused");
	CHECK(strcmp(copy.message, "origina") == 0);
	mh_error_copy(&copy, &copy, saved, sizeof(saved));
	CHECK(copy.code == MH_ERROR_IO && strcmp(saved, "origina") == 0);
	mh_error_copy(&copy, &copy, saved, 1);
	CHECK(copy.code == MH_ERROR_IO && saved[0] == 0);
	CHECK(mh_error_copy(NULL, &error, NULL, 0) == MH_ERROR_INVALID);
	mh_error_copy(&copy, &mh_success, saved, sizeof(saved));
	CHECK(copy.code == MH_ERROR_NONE && copy.message == NULL &&
		saved[0] == 0);
	mh_error_set(&error, MH_ERROR_MEMORY, NULL);
	CHECK(error.code == MH_ERROR_MEMORY && error.message != NULL);

#ifdef ENOENT
	CHECK(mh_error_system(NULL, ENOENT, "open failed") ==
		MH_ERROR_NOT_FOUND);
#endif
#ifdef EACCES
	CHECK(mh_error_system(NULL, EACCES, "open failed") ==
		MH_ERROR_PERMISSION);
#endif
#ifdef EEXIST
	CHECK(mh_error_system(NULL, EEXIST, "create failed") ==
		MH_ERROR_EXISTS);
#endif
#ifdef ENOMEM
	CHECK(mh_error_system(NULL, ENOMEM, "open failed") == MH_ERROR_MEMORY);
#endif
	CHECK(mh_error_system(NULL, 0, "unspecified I/O failure") ==
		MH_ERROR_IO);

	return 0;
}
