#ifndef MH_RENDER_MATRIX_H
#define MH_RENDER_MATRIX_H

extern const float mh_render_identity[16];

/* Column-major product a * b; out must not overlap either input. */
void	mh_render_matrix_multiply(float out[16], const float a[16],
	const float b[16]);

#endif
