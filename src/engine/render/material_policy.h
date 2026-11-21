#ifndef MH_MATERIAL_POLICY_H
#define MH_MATERIAL_POLICY_H

#include "engine/render/material_lists.h"
#include "engine/render/pipeline.h"
#include "interfaces/render/renderer.h"

/* Validated material inputs, independent of GPU resources and shader layout. */
struct mh_material_request {
	size_t list, texture, environment_texture, glenz_phong,
		glenz_reflection;
	int	glenz, untextured, shadow, skid, unavailable, reflections;
	int	lit, prelit, fog, alpha_test, prepared, environment;
};

struct mh_material_policy {
	enum mh_gpu_pipeline	pipeline;
	size_t			texture;

	/*
	 * Environment mode: none, reflection, or Phong. Other fields are
	 * booleans.
	 */
	int	environment, prelit, fog, alpha, distant, prepared, flat, opacity;
};

struct mh_material_sampling {
	enum mh_texture_filter	filter;
	int			repeat;
};

/* Shared material rules for Glide, OpenMH and unlit GPU presentation. */
void				mh_material_policy(
	const struct mh_material_request *request,
	struct mh_material_policy *output);
struct mh_material_sampling	mh_material_sampling(
	enum mh_texture_filter filter, int glenz, int environment, int alpha);

#endif
