#include <string.h>

#include "backends/render/webgl_frame.h"
#include "backends/render/webgl_powervr_order.h"
#include "engine/render/matrix.h"

static void
effect_parameters(struct mh_webgl_draw_call *call, enum mh_scene_effect effect,
	const struct mh_webgl_frame *frame, int powervr,
	const struct mh_effect_policy *policy)
{
	memset(call, 0, sizeof(*call));

	mh_webgl_frame_camera(frame, powervr, call->camera);

	if (effect == MH_SCENE_EFFECT_SKY)
		call->camera[MH_GPU_CAMERA_WATER_ENABLED] = 0;

	call->program = powervr	  ? MH_WEBGL_POWERVR
		: frame->direct3d ? MH_WEBGL_DIRECT3D
				: MH_WEBGL_MATERIAL;
	call->pipeline = policy->pipeline;
	call->material[MH_GPU_MATERIAL_PRELIT] = 1;
	call->material[MH_GPU_MATERIAL_FLAT] = policy->flat ? 1.0f : 0.0f;
	call->material[MH_GPU_MATERIAL_OPACITY] = policy->opacity ? 1.0f : 0.0f;
	call->material[MH_GPU_MATERIAL_ALPHA] =
		policy->particle_alpha ? MH_GPU_ALPHA_PARTICLE : 0.0f;
}

static void
bind_effect_mesh(struct mh_webgl_draw_call *call,
	const struct mh_webgl_scene *scene, size_t model,
	enum mh_scene_effect effect, const struct mh_webgl_frame *frame)
{
	const float *transform;

	call->geometry = &scene->meshes[model].geometry;

	if (effect == MH_SCENE_EFFECT_SKY ||
		effect == MH_SCENE_EFFECT_PARTICLES ||
		effect == MH_SCENE_EFFECT_HALOS) {
		memcpy(call->camera, mh_render_identity, 16 * sizeof(float));
		memcpy(call->camera + MH_GPU_CAMERA_VIEW, mh_render_identity,
			16 * sizeof(float));
		memcpy(call->camera + MH_GPU_CAMERA_MODEL, mh_render_identity,
			16 * sizeof(float));
		return;
	}

	transform = frame->models == NULL ? mh_render_identity
					: frame->models + model * 16;
	mh_render_matrix_multiply(call->camera, frame->matrix, transform);
	mh_render_matrix_multiply(
		call->camera + MH_GPU_CAMERA_VIEW, frame->view, transform);
	memcpy(call->camera + MH_GPU_CAMERA_MODEL, transform,
		16 * sizeof(float));
}

static enum mh_error_code
submit_effect_call(struct mh_webgl_drawing *drawing,
	const struct mh_webgl_mesh *mesh, const struct mh_draw *draw,
	struct mh_webgl_draw_call *call, unsigned int primitive_vertices,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	size_t first;

	if (MH_RENDER_BUILD_POWERVR && primitive_vertices != 0) {
		if (draw->count % primitive_vertices != 0 ||
			draw->first > mesh->geometry.count ||
			draw->count > mesh->geometry.count - draw->first)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid PowerVR effect quads");

		/* Each original effect quad is one PowerSGL call. */
		for (first = draw->first; first < draw->first + draw->count;
			first += primitive_vertices) {
			call->first = first;
			call->count = primitive_vertices;
			status = mh_webgl_order_defer(
				drawing->order, call, mesh, NULL, 0, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_webgl_draw_submit(drawing, call, error_out);
}

static enum mh_error_code
draw_effect_mesh(struct mh_webgl_drawing *drawing,
	const struct mh_webgl_scene *scene, const struct mh_webgl_frame *frame,
	size_t model, const struct mh_mesh_span *span,
	enum mh_scene_effect effect, struct mh_error *error_out)
{
	const struct mh_webgl_mesh *mesh;
	const struct mh_draw *draw;
	struct mh_webgl_draw_call call;
	struct mh_effect_policy policy;
	enum mh_render_profile profile;
	enum mh_error_code status;
	size_t i, texture;

	mesh = &scene->meshes[model];
	profile = scene->images.powervr ? MH_RENDER_POWERVR
		: frame->direct3d	? MH_RENDER_DIRECT3D
					: MH_RENDER_GLIDE;
	mh_scene_effect_policy(
		profile, frame->direct3d, frame->blur, effect, &policy);
	effect_parameters(&call, effect, frame, scene->images.powervr, &policy);
	bind_effect_mesh(&call, scene, model, effect, frame);

	for (i = span->draw_first; i < span->draw_first + span->draw_count;
		i++) {
		if (mesh->source->draw_hidden != NULL &&
			mesh->source->draw_hidden[i])
			continue;

		draw = &mesh->source->draws[i];
		texture = effect == MH_SCENE_EFFECT_SKIDS ||
				effect == MH_SCENE_EFFECT_SHADOWS
			? 0
			: draw->texture;

		if (texture >= mesh->source->texture_count ||
			mesh->images[texture] >= scene->images.count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing WebGL effect texture");

		call.texture = scene->images.items[mesh->images[texture]]
				.texture.handle;
		call.sampler = drawing->samplers[1][policy.repeat ? 1 : 0];

		if (policy.sky_sampler)
			call.sampler = mesh->source->environment_texture != 0 &&
					draw->texture ==
						mesh->source
							->environment_texture
				? drawing->samplers[1][1]
				: drawing->sky_sampler;

		call.first = draw->first;
		call.count = draw->count;

		status = submit_effect_call(drawing, mesh, draw, &call,
			policy.primitive_vertices, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_draw_effect(struct mh_webgl_drawing *drawing,
	const struct mh_webgl_scene *scene, const struct mh_webgl_frame *frame,
	enum mh_scene_effect effect, struct mh_error *error_out)
{
	struct mh_mesh_span span;
	enum mh_error_code status;
	size_t part, model;

	for (part = 0; part < mh_webgl_scene_part_count(scene); part++) {
		model = mh_webgl_scene_part(scene, part, &span);

		if (scene->meshes[model].hidden ||
			scene->meshes[model].effect != effect)
			continue;

		status = draw_effect_mesh(
			drawing, scene, frame, model, &span, effect, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
