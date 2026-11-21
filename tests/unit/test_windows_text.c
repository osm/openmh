#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "backends/storage/filesystem_windows_path.h"
#include "backends/text/windows.h"
#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "Windows text:%d: %s\n", __LINE__,     \
				#test);                                        \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	const char input[] = "C:/caf\303\251\\file";
	WCHAR invalid[] = {0xd800, 0}, sentinel, *wide = &sentinel;
	char marker, *text = &marker, *long_path;

	CHECK(mh_windows_text_to_wide("\300\257", &wide, NULL) ==
		MH_ERROR_INVALID);
	CHECK(wide == &sentinel);
	CHECK(mh_windows_text_to_utf8(invalid, &text, NULL) ==
		MH_ERROR_INVALID);
	CHECK(text == &marker);
	CHECK(mh_windows_text_to_wide(NULL, &wide, NULL) == MH_ERROR_INVALID);
	CHECK(wide == &sentinel);
	CHECK(mh_windows_text_to_wide(input, &wide, NULL) == MH_ERROR_NONE);
	CHECK(wide[2] == L'/' && wide[7] == L'\\');
	CHECK(mh_windows_text_to_utf8(wide, &text, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(text, input) == 0);
	free(text);
	free(wide);
	CHECK(mh_windows_path_to_wide(input, &wide, NULL) == MH_ERROR_NONE);
	CHECK(wide[2] == L'\\' && wide[7] == L'\\');
	CHECK(mh_windows_path_to_utf8(wide, &text, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(text, "C:/caf\303\251/file") == 0);
	free(text);
	free(wide);
	long_path = malloc(MH_WINDOWS_PATH_CHARACTERS + 1);
	CHECK(long_path != NULL);
	memset(long_path, 'a', MH_WINDOWS_PATH_CHARACTERS);
	long_path[MH_WINDOWS_PATH_CHARACTERS] = 0;
	wide = &sentinel;
	CHECK(mh_windows_path_to_wide(long_path, &wide, NULL) ==
		MH_ERROR_RANGE);
	CHECK(wide == &sentinel);
	long_path[MH_WINDOWS_PATH_CHARACTERS - 1] = 0;
	CHECK(mh_windows_path_to_wide(long_path, &wide, NULL) == MH_ERROR_NONE);
	free(wide);
	free(long_path);

	return 0;
}
