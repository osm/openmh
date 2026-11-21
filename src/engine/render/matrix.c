#include "engine/render/matrix.h"

const float mh_render_identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void
mh_render_matrix_multiply(float out[16], const float a[16], const float b[16])
{
	unsigned int row, col, k;

	for (col = 0; col < 4; col++)
		for (row = 0; row < 4; row++) {
			out[col * 4 + row] = 0;

			for (k = 0; k < 4; k++)
				out[col * 4 + row] +=
					a[k * 4 + row] * b[col * 4 + k];
		}
}
