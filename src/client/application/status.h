#ifndef MH_CLIENT_STATUS_H
#define MH_CLIENT_STATUS_H

/* One application step returns to its caller without running another frame. */
enum mh_app_status {
	MH_APP_FAILED = -1,
	MH_APP_DONE,
	MH_APP_RUNNING
};

#endif
