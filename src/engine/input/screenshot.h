#ifndef MH_INPUT_SCREENSHOT_H
#define MH_INPUT_SCREENSHOT_H

#include "engine/core/error.h"
#include "engine/input/binding.h"

enum {
	MH_SCREENSHOT_NAME_SIZE = 32
};

struct mh_screenshot {
	unsigned int	latched;
};

struct mh_screenshot_io {
	void		*context;
	enum mh_error_code (*exists)(void *context, const char *name,
		int	*present, struct mh_error *error_out);
	enum mh_error_code (*save)(
		void	*context, const char *name, struct mh_error *error_out);
};

struct mh_screenshot_result {
	int	saved;
	char	name[MH_SCREENSHOT_NAME_SIZE];
};

/*
 * Held binding, successful-save latch until release, first free motorN.tga
 * starting at zero for every attempt. Save failure leaves the latch clear so
 * held input can retry. Call after the frame is presented; the caller displays
 * the success notice. Stops at signed-32 index exhaustion and propagates I/O
 * errors. Failed calls preserve result; I/O failure leaves the latch clear.
 */
enum mh_error_code	mh_screenshot_poll(struct mh_screenshot *state,
	unsigned long binding, const struct mh_binding_input *input,
	const struct mh_screenshot_io *io, struct mh_screenshot_result *result,
	struct mh_error *error_out);

/*
 * Patched lobby screenshots use fixed F12 repeat events, not the race
 * binding/held key. Zero clears the latch; a nonzero event saves the first free
 * lobbyN.tga. Thus holding F12 can save again on a later repeat after a
 * zero-event frame.
 */
enum mh_error_code	mh_lobby_screenshot_poll(struct mh_screenshot *state,
	unsigned int key_event, const struct mh_screenshot_io *io,
	struct mh_screenshot_result *result, struct mh_error *error_out);

/*
 * Same input/retry rules, with a selected lowercase alphanumeric extension
 * of 1..8 characters. The default entry points above retain original TGA.
 */
enum mh_error_code	mh_screenshot_poll_format(struct mh_screenshot *state,
	unsigned long binding, const struct mh_binding_input *input,
	const char *extension, const struct mh_screenshot_io *io,
	struct mh_screenshot_result *result, struct mh_error *error_out);
enum mh_error_code	mh_lobby_screenshot_poll_format(
	struct mh_screenshot *state, unsigned int key_event,
	const char *extension, const struct mh_screenshot_io *io,
	struct mh_screenshot_result *result, struct mh_error *error_out);

#endif
