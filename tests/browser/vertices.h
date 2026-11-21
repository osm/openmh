#ifndef MH_TEST_WEBGL_VERTICES_H
#define MH_TEST_WEBGL_VERTICES_H

#include "backends/render/webgl_program.h"
#include "backends/render/webgl_vertex_array.h"

struct mh_test_webgl_vertices {
	GLuint				buffers[2], sentinel;
	struct mh_webgl_vertex_array	array;
};

void	mh_test_webgl_vertices_init(struct mh_test_webgl_vertices *vertices);
void	mh_test_webgl_vertices_free(struct mh_test_webgl_vertices *vertices);
int	mh_test_webgl_vertices_scene(struct mh_test_webgl_vertices *vertices,
	enum mh_webgl_program_kind kind, const float *triangle, int powervr);
void	mh_test_webgl_vertices_depth(struct mh_test_webgl_vertices *vertices,
	float depth);
int	mh_test_webgl_vertices_overlay(struct mh_test_webgl_vertices *vertices,
	enum mh_webgl_program_kind kind);
int	mh_test_webgl_vertices_lifetime(
	struct mh_test_webgl_vertices *vertices);

#endif
