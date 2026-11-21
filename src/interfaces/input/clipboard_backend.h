#ifndef MH_INPUT_CLIPBOARD_BACKEND_H
#define MH_INPUT_CLIPBOARD_BACKEND_H

#include "interfaces/input/clipboard.h"

/* Descriptor, table and context are borrowed and immutable through close. */
struct mh_clipboard_provider {
	const struct mh_clipboard_ops	*ops;
	void				*context;
};

struct mh_clipboard_ops {
	/*
	 * Begin one request; failed begin cleans partial state and leaves
	 * output unchanged. Successful requests always receive exactly one
	 * close. An asynchronous provider starts work here without waiting.
	 */
	enum mh_error_code (*begin)(
		void	*context, void **output, struct mh_error *error_out);

	/*
	 * Never wait for external completion. READY transfers one malloc-owned,
	 * NUL-terminated Windows-1252 string (including allocated empty text).
	 * PENDING/FAILED leave text unchanged. FAILED supplies a diagnostic;
	 * PENDING/READY clear it. No clipboard bytes escape before READY.
	 */
	enum mh_clipboard_status (*read)(void *context, void *request,
		char	**text, struct mh_error *error_out);

	/*
	 * Release a request in any state. Detach pending callbacks
	 * synchronously: a later platform completion must not touch the
	 * released request or its borrowed host context. This does not require
	 * cancelling the OS action. All operations run on the host's event
	 * thread.
	 */
	void		(*close)(void *context, void *request);
};

/* No I/O; invalid tables leave provider unchanged. */
enum mh_error_code	mh_clipboard_provider_init(
	struct mh_clipboard_provider *provider,
	const struct mh_clipboard_ops *ops, void *context,
	struct mh_error *error_out);
#endif
