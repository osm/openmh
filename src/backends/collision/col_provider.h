#ifndef MH_COLLISION_COL_PROVIDER_H
#define MH_COLLISION_COL_PROVIDER_H

struct mh_collision_decoder;
struct mh_collision_source;

/* Process-lifetime stateless decoder; retains original resource suffixes. */
const struct mh_collision_decoder	*mh_collision_col_decoder(void);

/* Ordinary external COL files. */
const struct mh_collision_source	*mh_collision_col_source(void);

#endif
