#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/input/sdl_provider.h"
#include "interfaces/input/clipboard_backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "native clipboard line %d: %s\n",      \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static const char utf8[] = "\303\245\303\244\303\266 \342\202\254";
	static const char game[] = "\345\344\366 \200";
	struct mh_clipboard *first, *second;

	CHECK(SDL_Init(SDL_INIT_VIDEO));
	CHECK(SDL_SetClipboardText(utf8));
	CHECK(mh_clipboard_open(mh_sdl_clipboard_provider(), &first, NULL) ==
		MH_ERROR_NONE);
	CHECK(SDL_SetClipboardText("second"));
	CHECK(mh_clipboard_open(mh_sdl_clipboard_provider(), &second, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_clipboard_step(first) == MH_CLIPBOARD_READY);
	CHECK(strcmp(mh_clipboard_result(first), game) == 0);
	CHECK(mh_clipboard_step(first) == MH_CLIPBOARD_READY);
	CHECK(mh_clipboard_step(second) == MH_CLIPBOARD_READY);
	CHECK(strcmp(mh_clipboard_result(second), "second") == 0);
	mh_clipboard_close(second);
	CHECK(strcmp(mh_clipboard_result(first), game) == 0);
	mh_clipboard_close(first);
	CHECK(SDL_SetClipboardText(""));
	CHECK(mh_clipboard_open(mh_sdl_clipboard_provider(), &first, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_clipboard_step(first) == MH_CLIPBOARD_READY);
	CHECK(mh_clipboard_result(first) != NULL &&
		*mh_clipboard_result(first) == 0);
	mh_clipboard_close(first);
	CHECK(mh_clipboard_open(mh_sdl_clipboard_provider(), &first, NULL) ==
		MH_ERROR_NONE);
	mh_clipboard_close(first);
	SDL_Quit();
	puts("Native clipboard preserves Windows-1252 conversion, snapshots, empty text and early close");

	return 0;
}
