#ifndef MH_SERVER_LOGGING_EVENTS_H
#define MH_SERVER_LOGGING_EVENTS_H

enum {
	MH_SERVER_EVENT_BYTES = 256
};

/* Borrowed synchronous sink. Text remains valid only during the call. */
struct mh_server_event_logger {
	void	(*write)(void *context, const char *text);
	void	*context;
};

/* Concatenate prefix/suffix, limited to 255 original text bytes. */
void	mh_server_event_log(const struct mh_server_event_logger *logger,
	const char *prefix, const char *suffix);

#endif
