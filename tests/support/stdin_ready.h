#ifndef MH_TEST_STDIN_READY_H
#define MH_TEST_STDIN_READY_H

/* Piped command drivers: 1 for bytes/EOF, 0 for pending, -1 for failure. */
int	mh_test_stdin_ready(void);

#endif
