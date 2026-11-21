#ifndef MH_TEST_NET_VIRTUAL_IO_H
#define MH_TEST_NET_VIRTUAL_IO_H
/* In-process datagrams only: never opens a socket or sends network traffic. */
/* Select the shared-port carrier before opening the first transport. */
void		mh_test_net_shared_port(void);
void		mh_test_net_faults(unsigned int drop_every,
	unsigned int duplicate_every, unsigned int delay_ms);
unsigned long	mh_test_net_drops(void);

/* Explicit deterministic frame clock; does not sleep or touch OS time. */
void	mh_test_net_advance(unsigned int milliseconds);
#endif
