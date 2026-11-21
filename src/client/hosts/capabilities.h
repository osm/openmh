#ifndef MH_CLIENT_HOSTS_CAPABILITIES_H
#define MH_CLIENT_HOSTS_CAPABILITIES_H

/*
 * Native host composition policy. Host adapters use these flags to select
 * optional providers; portable application owners use the selected descriptors
 * and their operation tables instead of this process-global query. Availability
 * stays fixed for an application lifetime; operation failures are reported by
 * the service itself. Resource/user-data views and readiness are required.
 */
enum mh_capability {
	MH_CAP_NETWORK,
	MH_CAP_VIDEO,
	MH_CAP_CLIPBOARD,
	/* In-game picture saves; diagnostic renderer captures are separate. */
	MH_CAP_SCREENSHOTS
};

int	mh_capability_available(enum mh_capability capability);

#endif
