#ifndef MH_RETRO_EXECUTABLE_PROVIDER_H
#define MH_RETRO_EXECUTABLE_PROVIDER_H

struct mh_retro_source;

/* Recover decoded parts from a verified motor.exe without writing files. */
const struct mh_retro_source	*mh_retro_executable_source(void);

#endif
