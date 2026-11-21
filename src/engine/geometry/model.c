#include <stdlib.h>
#include <string.h>

#include "engine/geometry/model.h"

void
mh_model_free(struct mh_model *model)
{
	size_t i;

	if (model == NULL)
		return;

	if (model->textures != NULL)
		for (i = 0; i < model->texture_count; i++)
			free(model->textures[i]);

	free(model->textures);
	free(model->faces);
	free(model->halos);
	memset(model, 0, sizeof(*model));
}
