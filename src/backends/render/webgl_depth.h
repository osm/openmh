#ifndef MH_WEBGL_DEPTH_H
#define MH_WEBGL_DEPTH_H

#include "backends/render/webgl_presentation.h"
#include "backends/render/webgl_program.h"
#include "backends/render/webgl_target.h"
#include "backends/render/webgl_vertex_array.h"
#include "engine/render/halo.h"

struct mh_webgl_depth {
	struct mh_webgl_download	download;
	struct mh_webgl_program		program;
	struct mh_webgl_vertex_array	array;
	struct mh_webgl_target		results;
	GLuint				vertices, parameters, sampler;
	float				*queries;
	unsigned char			*active;
	size_t				capacity, count;
};

/*
 * Zero-initialize storage; keep its context current through destruction.
 * Queue snapshots visibility on the GPU, read consumes it on the next frame.
 * Inactive entries retain the caller's history. A second queue cannot replace
 * unread results. No active probes means there is nothing to consume.
 * Queue owns draw state like the other WebGL passes, but restores framebuffer,
 * viewport and scissor enable. Read preserves framebuffer and pixel-pack state.
 */
enum mh_error_code	mh_webgl_depth_queue(struct mh_webgl_depth *depth,
	const struct mh_webgl_target *source,
	const struct mh_depth_probe *probes, size_t count,
	struct mh_error *error_out);
enum mh_error_code	mh_webgl_depth_read(struct mh_webgl_depth *depth,
	unsigned char *visible, size_t count, struct mh_error *error_out);

/* Live rendering retains history while the previous download is pending. */
enum mh_error_code	mh_webgl_depth_poll(struct mh_webgl_depth *depth,
	unsigned char *visible, size_t count, struct mh_error *error_out);
void			mh_webgl_depth_destroy(struct mh_webgl_depth *depth);

#endif
