#include <stdio.h>
#include <string.h>

#include "engine/core/text.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "ASCII text:%d: %s\n", __LINE__,       \
				#condition);                                   \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
byte_pairs(void)
{
	static const char upper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	static const char lower[] = "abcdefghijklmnopqrstuvwxyz";
	unsigned int expected[256], i, j;
	char left[2], right[2], folded[2];
	int order;

	for (i = 0; i < 256; i++)
		expected[i] = i;

	for (i = 0; i < sizeof(upper) - 1; i++)
		expected[(unsigned char)upper[i]] = (unsigned char)lower[i];

	left[1] = right[1] = 0;

	for (i = 0; i < 256; i++) {
		left[0] = (char)i;
		folded[0] = (char)i;
		folded[1] = 0;
		mh_ascii_lower(folded);
		CHECK((unsigned char)folded[0] == expected[i] &&
			folded[1] == 0);

		for (j = 0; j < 256; j++) {
			right[0] = (char)j;
			order = mh_ascii_compare(left, right);
			CHECK((order < 0) == (expected[i] < expected[j]));
			CHECK((order > 0) == (expected[i] > expected[j]));
			CHECK(mh_ascii_equal(left, right) ==
				(expected[i] == expected[j]));
			CHECK(mh_ascii_equal_n(left, right, 1) ==
				(expected[i] == expected[j]));
		}
	}

	return 0;
}

static int
bounded_text(void)
{
	char storage[12], before[12], source[10];
	char unterminated[3] = {'a', 'b', 'c'};
	struct mh_error error;
	size_t capacity, length, i;
	enum mh_error_code status;

	for (capacity = 0; capacity <= 9; capacity++) {
		for (length = 0; length <= 9; length++) {
			memset(source, 'x', length);
			source[length] = 0;
			memset(storage, '#', sizeof(storage));
			memcpy(before, storage, sizeof(before));
			status = mh_text_copy(
				storage + 1, capacity, source, &error);
			CHECK(error.code == status);
			CHECK(status ==
				(length < capacity ? MH_ERROR_NONE
						: MH_ERROR_RANGE));

			if (status == MH_ERROR_NONE) {
				CHECK(strcmp(storage + 1, source) == 0);
				CHECK(storage[0] == '#');

				for (i = length + 2; i < sizeof(storage); i++)
					CHECK(storage[i] == '#');
			} else
				CHECK(memcmp(storage, before,
					sizeof(storage)) == 0);

			memset(storage, '#', sizeof(storage));
			memcpy(storage + 1, "ab", 3);
			memcpy(before, storage, sizeof(before));
			status = mh_text_append(
				storage + 1, capacity, source, &error);
			CHECK(error.code == status);
			CHECK(status ==
				(length + 2 < capacity ? MH_ERROR_NONE
						: MH_ERROR_RANGE));

			if (status == MH_ERROR_NONE) {
				CHECK(memcmp(storage, "#ab", 3) == 0);
				CHECK(strcmp(storage + 3, source) == 0);

				for (i = length + 4; i < sizeof(storage); i++)
					CHECK(storage[i] == '#');
			} else
				CHECK(memcmp(storage, before,
					sizeof(storage)) == 0);
		}
	}

	CHECK(mh_text_copy(storage, 3, unterminated, NULL) == MH_ERROR_RANGE);
	CHECK(mh_text_append(unterminated, 3, "", NULL) == MH_ERROR_RANGE);
	CHECK(memcmp(unterminated, "abc", 3) == 0);
	CHECK(mh_text_copy(NULL, 1, "", NULL) == MH_ERROR_INVALID);
	CHECK(mh_text_copy(storage, sizeof(storage), NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_text_append(NULL, 1, "", NULL) == MH_ERROR_INVALID);
	CHECK(mh_text_append(storage, sizeof(storage), NULL, NULL) ==
		MH_ERROR_INVALID);
	memcpy(storage, "abcd", 5);
	CHECK(mh_text_copy(storage + 1, 5, storage, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(storage, "aabcd") == 0);
	CHECK(mh_text_copy(storage, 5, storage + 1, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(storage, "abcd") == 0);
	CHECK(mh_text_append(storage, sizeof(storage), storage, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(storage, "abcdabcd") == 0);

	return 0;
}

static int
fields(void)
{
	char text[2];
	unsigned int byte;

	CHECK(!mh_text_field_valid(NULL, 2));
	CHECK(!mh_text_field_valid("", 0));
	CHECK(mh_text_field_valid("", 1));
	CHECK(!mh_text_field_valid("ab", 2));
	CHECK(mh_text_field_valid("ab", 3));
	text[1] = 0;

	for (byte = 1; byte < 256; byte++) {
		text[0] = (char)byte;
		CHECK(mh_text_field_valid(text, sizeof(text)) == (byte >= 32));
	}

	return 0;
}

int
main(void)
{
	const char left[4] = {'G', 'R', 'E', 'M'};
	const char right[4] = {'g', 'r', 'e', 'm'};
	const char terminated[] = {'a', 0, 'x'};
	const char other_tail[] = {'A', 0, 'y'};

	CHECK(byte_pairs() == 0);
	CHECK(fields() == 0);
	CHECK(bounded_text() == 0);
	CHECK(mh_ascii_equal("", ""));
	CHECK(!mh_ascii_equal("", "A"));
	CHECK(mh_ascii_equal("Profile.CFG", "profile.cfg"));
	CHECK(!mh_ascii_equal("profile", "profiles"));
	CHECK(mh_ascii_compare("profile", "profiles") < 0);
	CHECK(mh_ascii_compare("profiles", "profile") > 0);
	CHECK(!mh_ascii_equal("[", "{"));
	CHECK(!mh_ascii_equal("\304", "\344"));
	CHECK(mh_ascii_equal_n(NULL, NULL, 0));
	CHECK(mh_ascii_equal_n(left, right, sizeof(left)));
	CHECK(mh_ascii_equal_n("GREMLIN", right, sizeof(right)));
	CHECK(!mh_ascii_equal_n("GRE", right, sizeof(right)));
	CHECK(!mh_ascii_equal_n(right, "GRE", sizeof(right)));
	CHECK(mh_ascii_equal_n(terminated, other_tail, sizeof(terminated)));
	CHECK(mh_ascii_equal_n("", "", 16));

	return 0;
}
