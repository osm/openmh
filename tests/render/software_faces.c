#include <float.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/software_face.h"
#include "backends/render/software_palette.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
guards(void)
{
	struct mh_software_clipped_face face;
	struct mh_software_material material;
	struct mh_software_texture texture;
	struct mh_software_triangle triangles[2], saved[2];
	unsigned int i, triangle_count;

	memset(&face, 0, sizeof(face));
	memset(&material, 0, sizeof(material));
	memset(&texture, 0, sizeof(texture));
	face.flags =
		MH_NEAR_QUAD | MH_NEAR_TEXTURED | MH_SOFTWARE_FACE_PERSPECTIVE;
	face.count = 4;
	material.texture = &texture;
	material.mapping = MH_TEXTURE_SELECTIVE;
	material.perspective_shift = 5;

	for (i = 0; i < 4; i++) {
		face.corners[i].xy[0] = (float)i;
		face.corners[i].weight = i == 3 ? .5f : .25f;
		face.corners[i].rgb[0] = (float)(i * 20);
	}

	CHECK(mh_software_face_prepare(&face, &material, triangles,
		&triangle_count, NULL) == MH_ERROR_NONE);
	CHECK(triangle_count == 2 && triangles[0].xy[1][0] == 2 &&
		triangles[0].xy[2][0] == 3);
	CHECK(triangles[1].xy[1][0] == 1 && triangles[1].xy[2][0] == 2);
	CHECK(triangles[0].perspective_shift == 5 &&
		triangles[1].perspective_shift == 0);
	CHECK(triangles[0].color[1][0] == 0);
	face.flags |= MH_SOFTWARE_FACE_ADDITIVE;
	CHECK(mh_software_face_prepare(&face, &material, triangles,
		&triangle_count, NULL) == MH_ERROR_NONE);
	CHECK(triangles[0].additive && triangles[1].additive &&
		triangles[0].perspective_shift == 0 &&
		triangles[1].perspective_shift == 0);
	face.flags &= ~MH_SOFTWARE_FACE_ADDITIVE;
	face.flags |= MH_SOFTWARE_FACE_CUTOUT;
	texture.zero_transparent = 1;
	CHECK(mh_software_face_prepare(&face, &material, triangles,
		&triangle_count, NULL) == MH_ERROR_NONE);
	CHECK(triangles[0].perspective_shift == 0 &&
		triangles[1].perspective_shift == 0);
	memcpy(saved, triangles, sizeof(saved));
	triangle_count = 77;
	face.count = 3;
	CHECK(mh_software_face_prepare(&face, &material, triangles,
		&triangle_count, NULL) == MH_ERROR_INVALID);
	CHECK(triangle_count == 77 &&
		memcmp(saved, triangles, sizeof(saved)) == 0);
	face.count = 4;
	face.flags |= 0x80;
	CHECK(mh_software_face_prepare(&face, &material, triangles,
		&triangle_count, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(triangle_count == 77 &&
		memcmp(saved, triangles, sizeof(saved)) == 0);
	face.flags &= ~0x80U;
	material.texture = NULL;
	CHECK(mh_software_face_prepare(&face, &material, triangles,
		&triangle_count, NULL) == MH_ERROR_INVALID);
	CHECK(triangle_count == 77 &&
		memcmp(saved, triangles, sizeof(saved)) == 0);
	material.texture = &texture;
	texture.zero_transparent = 0;
	CHECK(mh_software_face_prepare(&face, &material, triangles,
		&triangle_count, NULL) == MH_ERROR_INVALID);
	CHECK(triangle_count == 77 &&
		memcmp(saved, triangles, sizeof(saved)) == 0);

	/* Saturation belongs to smooth dispatch, after clipping. */
	face.flags = MH_NEAR_QUAD | MH_NEAR_VERTEX_COLOR;
	material.texture = NULL;
	face.corners[2].rgb[0] = 510;
	CHECK(mh_software_face_prepare(&face, &material, triangles,
		&triangle_count, NULL) == MH_ERROR_NONE);
	CHECK(triangle_count == 2 && triangles[0].color[1][0] == 255 &&
		triangles[1].color[2][0] == 255);
	memcpy(saved, triangles, sizeof(saved));
	face.corners[2].rgb[0] = FLT_MAX;
	triangle_count = 77;
	CHECK(mh_software_face_prepare(&face, &material, triangles,
		&triangle_count, NULL) == MH_ERROR_RANGE);
	CHECK(triangle_count == 77 &&
		memcmp(saved, triangles, sizeof(saved)) == 0);
	face.corners[2].rgb[0] = -1;
	CHECK(mh_software_face_prepare(&face, &material, triangles,
		&triangle_count, NULL) == MH_ERROR_RANGE);
	CHECK(triangle_count == 77 &&
		memcmp(saved, triangles, sizeof(saved)) == 0);

	return 0;
}

int
main(void)
{
	if (guards() != 0)
		return 1;

	return 0;
}
