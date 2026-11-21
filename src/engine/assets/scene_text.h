#ifndef MH_ASSETS_SCENE_TEXT_H
#define MH_ASSETS_SCENE_TEXT_H

#include <stddef.h>

#define MH_SCENE_TEXT_LINE_BYTES 1024

/* Private text cursor shared by the LWSC and LWMO asset readers. */
struct mh_scene_text {
	const unsigned char	*text;
	size_t			size;
	size_t			cursor;
	char			line[MH_SCENE_TEXT_LINE_BYTES];
};

/* Skip blank lines and trim spaces/tabs; 1=line, 0=end, -1=invalid input. */
int	mh_scene_text_next(struct mh_scene_text *input);

/* Read exactly count finite values within the native format-reader bound. */
int	mh_scene_text_numbers(const char *text, double *values,
	unsigned int count);

#endif
