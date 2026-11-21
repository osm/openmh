#include "backends/render/webgl_error.h"
#include "backends/render/webgl_mesh.h"

static void
upload(const struct mh_webgl_mesh *mesh)
{
	if (mesh->vertices_pending) {
		glBindBuffer(GL_ARRAY_BUFFER, mesh->geometry.vertices);
		glBufferSubData(GL_ARRAY_BUFFER, 0,
			mesh->vertex_count * sizeof(*mesh->upload),
			mesh->upload);
	}

	if (mesh->glenz_pending) {
		glBindBuffer(GL_ARRAY_BUFFER, mesh->geometry.glenz);
		glBufferSubData(GL_ARRAY_BUFFER, 0,
			mesh->vertex_count * sizeof(*mesh->glenz_coordinates),
			mesh->glenz_coordinates);
	}
}

enum mh_error_code
mh_webgl_meshes_upload(
	struct mh_webgl_mesh *meshes, size_t count, struct mh_error *error_out)
{
	GLint previous;
	size_t i;
	int pending;
	enum mh_error_code status;

	if (count != 0 && meshes == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL upload meshes");

	pending = 0;

	for (i = 0; i < count; i++) {
		if (!meshes[i].vertices_pending && !meshes[i].glenz_pending)
			continue;

		if (meshes[i].vertex_count != meshes[i].geometry.count ||
			(meshes[i].vertices_pending &&
				(meshes[i].geometry.vertices == 0 ||
					meshes[i].upload == NULL)) ||
			(meshes[i].glenz_pending &&
				(meshes[i].geometry.glenz == 0 ||
					meshes[i].glenz_coordinates == NULL)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid pending WebGL mesh upload");

		pending = 1;
	}

	if (!pending)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/* Checking each mesh separately stalls the browser's graphics queue. */
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);

	for (i = 0; i < count; i++)
		upload(&meshes[i]);

	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previous);
	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < count; i++)
		meshes[i].vertices_pending = meshes[i].glenz_pending = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
