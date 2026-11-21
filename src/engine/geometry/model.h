#ifndef MH_ENGINE_GEOMETRY_MODEL_H
#define MH_ENGINE_GEOMETRY_MODEL_H

#include "interfaces/geometry/model.h"

/* Accepts NULL or a zero-initialized owner; clears the released value. */
void	mh_model_free(struct mh_model *model);

#endif
