#ifndef MH_EXECUTABLE_IDENTITY_H
#define MH_EXECUTABLE_IDENTITY_H

/* Whole-file motor.exe identity; offsets and content policy live separately. */
struct mh_executable_identity {
	unsigned long	size;
	const char	*sha256;
};

/* Supported executable identities. */
extern const struct mh_executable_identity mh_executable_retail_12;
extern const struct mh_executable_identity mh_executable_retail_30;
extern const struct mh_executable_identity mh_executable_retail_31;
extern const struct mh_executable_identity mh_executable_demo_software_first;
extern const struct mh_executable_identity mh_executable_demo_software_second;
extern const struct mh_executable_identity mh_executable_demo_glide_first;
extern const struct mh_executable_identity mh_executable_demo_glide_second;
extern const struct mh_executable_identity mh_executable_demo_mplayer;

#endif
