#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/tron.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_faces(void)
{
	struct mh_scene_vertex corners[4];
	struct mh_tron_face face, saved;
	struct mh_tron_line_vertex lines[8], previous[8];
	unsigned int corner, line_count;

	memset(corners, 0, sizeof(corners));

	for (corner = 0; corner < 4; corner++) {
		corners[corner].xy[0] = corner == 1 || corner == 2 ? 90 : 10;
		corners[corner].xy[1] = corner >= 2 ? 90 : 10;
		corners[corner].depth = .5f;
		corners[corner].reciprocal_w = .25f;
		corners[corner].rgb[0] = 100;
		corners[corner].rgb[1] = 50;
		corners[corner].rgb[2] = 50;
		corners[corner].uv[0] = 2;
		corners[corner].uv[1] = -1;
		corners[corner].visibility = 127;
		corners[corner].background = 64;
	}

	CHECK(mh_tron_prepare_face(MH_SCENE_TEXTURED, 4, 1, corners, &face,
		NULL) == MH_ERROR_NONE);
	CHECK(face.vertices[0].line.rgb[2] == 66 &&
		face.vertices[0].line.depth == 32768);
	CHECK(face.vertices[0].line.alpha == 128 &&
		face.vertices[0].texture_w[0] == 128);
	CHECK(face.vertices[0].texture_w[1] == -64);
	CHECK(mh_tron_face_outline(&face, 100, 100, lines, &line_count, NULL) ==
			MH_ERROR_NONE &&
		line_count == 4);

	for (corner = 0; corner < line_count; corner++) {
		CHECK(memcmp(lines[corner * 2].xy, corners[corner].xy,
			sizeof(corners[corner].xy)) == 0);
		CHECK(memcmp(lines[corner * 2 + 1].xy,
			corners[(corner + 1) % 4].xy,
			sizeof(corners[corner].xy)) == 0);
		CHECK(lines[corner * 2].rgb[2] == 255 &&
			lines[corner * 2].depth == 32768);
	}

	saved = face;
	memcpy(previous, lines, sizeof(previous));
	corners[3].depth = 2;
	CHECK(mh_tron_prepare_face(MH_SCENE_TEXTURED, 4, 1, corners, &face,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&face, &saved, sizeof(face)) == 0);
	CHECK(mh_tron_prepare_face(MH_SCENE_TEXTURED, 5, 1, corners, &face,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_tron_prepare_face(MH_SCENE_PASSES, 4, 1, corners, &face,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_tron_prepare_face(MH_SCENE_TEXTURED, 4, 2, corners, &face,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_tron_prepare_face(MH_SCENE_TEXTURED, 4, 1, NULL, &face,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_tron_prepare_face(MH_SCENE_TEXTURED, 4, 1, corners, NULL,
		NULL) != MH_ERROR_NONE);
	corners[3].depth = .5f;
	corners[3].reciprocal_w = 0;
	CHECK(mh_tron_prepare_face(MH_SCENE_TEXTURED, 4, 1, corners, &face,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&face, &saved, sizeof(face)) == 0);
	face.vertices[3].line.depth = 1e10f;
	line_count = 99;
	CHECK(mh_tron_face_outline(&face, 100, 100, lines, &line_count, NULL) !=
			MH_ERROR_NONE &&
		line_count == 99);
	CHECK(memcmp(previous, lines, sizeof(lines)) == 0);
	corners[3].reciprocal_w = .25f;
	CHECK(mh_tron_prepare_face(MH_SCENE_ALPHA, 4, 0, corners, &face,
		NULL) == MH_ERROR_NONE);
	CHECK(face.vertices[0].line.rgb[2] == 16.5f);
	CHECK(mh_tron_face_outline(&face, 100, 100, lines, &line_count, NULL) ==
			MH_ERROR_NONE &&
		line_count == 0);
	CHECK(memcmp(previous, lines, sizeof(lines)) == 0);
	CHECK(mh_tron_style(MH_SCENE_PASSES) == NULL);

	return 0;
}

int
main(void)
{
	struct mh_tron_line_vertex input[2], output[2], saved[2], reverse[2];
	unsigned int channel;
	int visible;

	memset(input, 0, sizeof(input));
	memset(output, 0x55, sizeof(output));
	input[0].xy[0] = -50;
	input[1].xy[0] = 50;
	input[1].xy[1] = 100;

	for (channel = 0; channel < 3; channel++)
		input[1].rgb[channel] = 100;

	input[0].depth = 10;
	input[1].depth = 20;
	input[1].alpha = 200;
	input[0].reciprocal_w = 1;
	input[1].reciprocal_w = 3;
	CHECK(mh_tron_clip_line(100, 100, input, output, &visible, NULL) ==
			MH_ERROR_NONE &&
		visible);
	CHECK(output[0].xy[0] == 0 && output[0].xy[1] == 50);
	CHECK(output[0].depth == 15 && output[0].alpha == 100 &&
		output[0].reciprocal_w == 2);

	for (channel = 0; channel < 3; channel++)
		CHECK(output[0].rgb[channel] == 50);

	CHECK(memcmp(&output[1], &input[1], sizeof(input[1])) == 0);
	reverse[0] = input[1];
	reverse[1] = input[0];
	CHECK(mh_tron_clip_line(100, 100, reverse, saved, &visible, NULL) ==
			MH_ERROR_NONE &&
		visible);
	CHECK(memcmp(&saved[0], &output[1], sizeof(saved[0])) == 0);
	CHECK(memcmp(&saved[1], &output[0], sizeof(saved[1])) == 0);

	/* Fully interior coordinates bypass the snapping branch. */
	input[0].xy[0] = .33333334f;
	input[1].xy[1] = 99;
	CHECK(mh_tron_clip_line(100, 100, input, output, &visible, NULL) ==
			MH_ERROR_NONE &&
		visible);
	CHECK(memcmp(input, output, sizeof(input)) == 0);
	memcpy(saved, output, sizeof(saved));
	input[0].xy[0] = 101;
	input[1].xy[0] = 200;
	CHECK(mh_tron_clip_line(100, 100, input, output, &visible, NULL) ==
			MH_ERROR_NONE &&
		!visible);
	CHECK(memcmp(saved, output, sizeof(saved)) == 0);

	/* Parallel outside edges reach division by zero in the original. */
	input[0].xy[0] = input[1].xy[0] = -1e-5f;
	CHECK(mh_tron_clip_line(100, 100, input, output, &visible, NULL) ==
			MH_ERROR_NONE &&
		!visible);
	CHECK(memcmp(saved, output, sizeof(saved)) == 0);
	input[0].xy[0] = input[1].xy[0] = 100;
	CHECK(mh_tron_clip_line(100, 100, input, output, &visible, NULL) ==
			MH_ERROR_NONE &&
		!visible);
	CHECK(memcmp(saved, output, sizeof(saved)) == 0);
	visible = 9;
	CHECK(mh_tron_clip_line(0, 100, input, output, &visible, NULL) !=
			MH_ERROR_NONE &&
		visible == 9);
	CHECK(mh_tron_clip_line(100, 16385, input, output, &visible, NULL) !=
			MH_ERROR_NONE &&
		visible == 9);
	CHECK(mh_tron_clip_line(100, 100, NULL, output, &visible, NULL) !=
			MH_ERROR_NONE &&
		visible == 9);
	CHECK(mh_tron_clip_line(100, 100, input, NULL, &visible, NULL) !=
			MH_ERROR_NONE &&
		visible == 9);
	CHECK(mh_tron_clip_line(100, 100, input, output, NULL, NULL) !=
		MH_ERROR_NONE);
	input[1].rgb[2] = 1e10f;
	CHECK(mh_tron_clip_line(100, 100, input, output, &visible, NULL) !=
			MH_ERROR_NONE &&
		visible == 9);
	CHECK(memcmp(saved, output, sizeof(saved)) == 0);
	CHECK(check_faces() == 0);
	puts("TRON faces, perimeter clipping, attribute interpolation and atomic rejection pass");

	return 0;
}
