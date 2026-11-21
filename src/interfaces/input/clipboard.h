#ifndef MH_INPUT_CLIPBOARD_H
#define MH_INPUT_CLIPBOARD_H

#include "engine/core/error.h"

struct mh_clipboard;
struct mh_clipboard_provider;

enum mh_clipboard_status {
	MH_CLIPBOARD_FAILED = -1,
	MH_CLIPBOARD_PENDING,
	MH_CLIPBOARD_READY
};

/*
 * Start one text read. NULL provider reports UNSUPPORTED. Successful open owns
 * a request and borrows its provider until close; failure leaves output alone.
 * Asynchronous providers return without waiting for external completion; step
 * polls that completion without blocking. Immediate providers may finish the
 * fetch during open. The host must meet its platform's activation
 * and permission requirements when starting a request.
 */
enum mh_error_code	mh_clipboard_open(
	const struct mh_clipboard_provider *provider,
	struct mh_clipboard **output, struct mh_error *error_out);

/* Terminal steps are idempotent and do not call the provider again. */
enum mh_clipboard_status	mh_clipboard_step(
	struct mh_clipboard *clipboard);

/*
 * NUL-terminated Windows-1252 bytes, borrowed until close; NULL unless ready.
 */
const char	*mh_clipboard_result(const struct mh_clipboard *clipboard);

/* Failure diagnostics are copied and remain valid until close. */
enum mh_error_code	mh_clipboard_error(const struct mh_clipboard *clipboard,
	struct mh_error *error_out);

/* Detach outstanding callbacks before releasing provider/context ownership. */
void	mh_clipboard_close(struct mh_clipboard *clipboard);

#endif
