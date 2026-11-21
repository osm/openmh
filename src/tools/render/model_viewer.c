#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/storage/filesystem_file_read.h"
#include "client/diagnostics/asset_config_load.h"
#include "client/diagnostics/asset_mesh_load.h"
#include "client/diagnostics/asset_path.h"
#include "client/diagnostics/model_viewer.h"
#include "engine/assets/mesh_load.h"
#include "engine/assets/myo.h"
#include "engine/assets/path_tracks.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/geometry/model.h"
#include "engine/render/mesh.h"
#include "engine/storage/asset_path.h"

#define PI 3.14159265358979323846
#define MAX_ANIMATION_SECONDS 1e9
#define MAX_PITCH_DEGREES 80.0
#define MAX_YAW_DEGREES 3600.0

enum {
	ASSET_PATH_BYTES = 1024,
	ARCHIVE_NAME_BYTES = MH_TRACK_PATH_SIZE + 32,
	MESH_NAME_BYTES = 256
};

enum parse_result {
	PARSE_ERROR,
	PARSE_READY,
	PARSE_HELP
};

struct model_selection {
	struct mh_model_viewer_options	view;
	const char			*root, *model_path, *archive_path;
	long				car, track;
	int	check, track_selected, world, standard_world, no_scenery;
};

static void
usage(void)
{
	puts("Usage: mh-model-viewer [--car NUMBER] --gamedir GAME_ROOT [--textures PDI]");
	puts("       mh-model-viewer MODEL.MYO TEXTURES.PDI");
	puts("       mh-model-viewer --gamedir GAME_ROOT --world [--track NUMBER] [--standard-world]");
	puts("         --no-scenery omits the scene's objects.");
	puts("Options: --capture OUTPUT.bmp | --headless OUTPUT.bmp | --check");
	puts("         --track NUMBER --no-reflections --yaw DEGREES --pitch DEGREES");
	puts("         --time SECONDS --paused (scenery playback/captures)");
	puts("Default model: assembled car01 in reset pose.");
	puts("--world previews baked track geometry and animated scenery (hardware variant by default).");
	puts("Single models use reflections only when --track is supplied.");
	puts("Drag/arrows: orbit; wheel: zoom; E: reflections; R: reset camera; Esc: quit.");
	puts("Space: pause/resume scenery; Home: restart scenery at zero seconds.");
}

static enum parse_result
read_options(int argc, char *argv[], struct model_selection *selection)
{
	char *end;
	double degrees;
	int car_selected, pitch_selected, i;

	selection->root = NULL;
	selection->model_path = NULL;
	selection->archive_path = NULL;
	selection->view.capture_path = NULL;
	selection->view.headless = 0;
	selection->check = 0;
	car_selected = 0;
	selection->track_selected = 0;
	selection->world = 0;
	selection->standard_world = 0;
	selection->no_scenery = 0;
	pitch_selected = 0;
	selection->car = 1;
	selection->track = 1;
	selection->view.reflections = 1;
	selection->view.yaw = 0.7;
	selection->view.pitch = 0.35;
	selection->view.animation_seconds = 0.0;
	selection->view.paused = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0) {
			usage();
			return PARSE_HELP;
		} else if (strcmp(argv[i], "--check") == 0) {
			selection->check = 1;
		} else if (strcmp(argv[i], "--no-reflections") == 0) {
			selection->view.reflections = 0;
		} else if (strcmp(argv[i], "--world") == 0) {
			selection->world = 1;
		} else if (strcmp(argv[i], "--standard-world") == 0) {
			selection->standard_world = 1;
		} else if (strcmp(argv[i], "--no-scenery") == 0) {
			selection->no_scenery = 1;
		} else if (strcmp(argv[i], "--paused") == 0) {
			selection->view.paused = 1;
		} else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
			errno = 0;
			selection->view.animation_seconds =
				strtod(argv[++i], &end);

			if (errno != 0 || end == argv[i] || *end != 0 ||
				!(selection->view.animation_seconds >= 0 &&
					selection->view.animation_seconds <=
						MAX_ANIMATION_SECONDS)) {
				fprintf(stderr,
					"Time must be a finite number between 0 and 1e9 seconds.\n");
				return PARSE_ERROR;
			}
		} else if (strcmp(argv[i], "--pitch") == 0 && i + 1 < argc) {
			errno = 0;
			degrees = strtod(argv[++i], &end);

			if (errno != 0 || end == argv[i] || *end != 0 ||
				!(degrees >= -MAX_PITCH_DEGREES &&
					degrees <= MAX_PITCH_DEGREES)) {
				fprintf(stderr,
					"Pitch must be between -80 and 80 degrees.\n");
				return PARSE_ERROR;
			}

			selection->view.pitch = degrees * PI / 180.0;
			pitch_selected = 1;
		} else if (strcmp(argv[i], "--yaw") == 0 && i + 1 < argc) {
			errno = 0;
			degrees = strtod(argv[++i], &end);

			if (errno != 0 || end == argv[i] || *end != 0 ||
				!(degrees >= -MAX_YAW_DEGREES &&
					degrees <= MAX_YAW_DEGREES)) {
				fprintf(stderr,
					"Yaw must be a finite number between -3600 and 3600 degrees.\n");
				return PARSE_ERROR;
			}

			selection->view.yaw = degrees * PI / 180.0;
		} else if (strcmp(argv[i], "--track") == 0 && i + 1 < argc) {
			selection->track = strtol(argv[++i], &end, 10);

			if (*end != 0 || selection->track < 1 ||
				selection->track > MH_TRACK_CATALOG_LIMIT) {
				fprintf(stderr,
					"Track number must be between 1 and 8.\n");
				return PARSE_ERROR;
			}

			selection->track_selected = 1;
		} else if (strcmp(argv[i], "--car") == 0 && i + 1 < argc) {
			selection->car = strtol(argv[++i], &end, 10);

			if (*end != 0 || selection->car < 1 ||
				selection->car > MH_CAR_CATALOG_LIMIT) {
				fprintf(stderr,
					"Car number must be between 1 and 10.\n");
				return PARSE_ERROR;
			}

			car_selected = 1;
		} else if (strcmp(argv[i], "--gamedir") == 0 && i + 1 < argc) {
			selection->root = argv[++i];
		} else if (strcmp(argv[i], "--textures") == 0 && i + 1 < argc) {
			selection->archive_path = argv[++i];
		} else if ((strcmp(argv[i], "--capture") == 0 ||
				strcmp(argv[i], "--headless") == 0) &&
			i + 1 < argc) {
			if (selection->view.capture_path != NULL) {
				fprintf(stderr, "Choose one capture mode.\n");
				return PARSE_ERROR;
			}

			selection->view.headless =
				strcmp(argv[i], "--headless") == 0;
			selection->view.capture_path = argv[++i];
		} else if (argv[i][0] != '-' && selection->model_path == NULL &&
			i + 1 < argc) {
			selection->model_path = argv[i++];
			selection->archive_path = argv[i];
		} else {
			usage();
			return PARSE_ERROR;
		}
	}

	if ((car_selected && selection->model_path != NULL) ||
		(selection->check && selection->view.capture_path != NULL) ||
		(selection->world &&
			(car_selected || selection->model_path != NULL)) ||
		((selection->standard_world || selection->no_scenery) &&
			!selection->world)) {
		fprintf(stderr, "Conflicting inspection modes.\n");
		return PARSE_ERROR;
	}

	if (selection->root == NULL &&
		(selection->model_path == NULL || selection->track_selected)) {
		fprintf(stderr,
			"Supply --gamedir for car, world or track assets.\n");
		return PARSE_ERROR;
	}

	if (selection->world && !pitch_selected)
		selection->view.pitch = 1.0;

	return PARSE_READY;
}

static enum mh_error_code
load_model_file(const char *path, const struct mh_image_collection *archive,
	struct mh_mesh *mesh, struct mh_error *error_out)
{
	struct mh_file file;
	struct mh_model model;
	enum mh_error_code status;

	status = mh_file_read(path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&model, 0, sizeof(model));
	status = mh_model_myo_decode(file.data, file.size, &model, error_out);
	free(file.data);

	if (status == MH_ERROR_NONE)
		status = mh_mesh_build(mh_image_fallback_decoder(), &model,
			archive, mesh, error_out);

	mh_model_free(&model);

	return status;
}

static int
load_mesh(const struct model_selection *selection, struct mh_mesh *mesh,
	char name[MESH_NAME_BYTES])
{
	struct mh_file archive_file;
	struct mh_track track;
	struct mh_image_collection *archive = NULL;
	const char *archive_path;
	struct mh_error error;
	enum mh_error_code status;
	char archive_resolved[ASSET_PATH_BYTES],
		archive_relative[ARCHIVE_NAME_BYTES];

	archive_path = selection->archive_path;

	if (archive_path == NULL) {
		status = mh_track_config_load(selection->root,
			(unsigned int)selection->track, &track, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Track configuration: %s\n",
				error.message);
			return 0;
		}

		sprintf(archive_relative,
			MH_ASSET_TRACK_TEXTURES_PRIMARY_FORMAT,
			track.base_path);
		status = mh_asset_path(selection->root, archive_relative,
			archive_resolved, sizeof(archive_resolved), &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Texture path: %s\n", error.message);
			return 0;
		}

		archive_path = archive_resolved;
	}

	status = mh_file_read(archive_path, &archive_file, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s: %s\n", archive_path, error.message);
		return 0;
	}

	status = mh_image_pdi_open(
		archive_file.data, archive_file.size, &archive, &error);

	if (status == MH_ERROR_NONE) {
		if (selection->world) {
			status = mh_world_mesh_load(selection->root,
				(unsigned int)selection->track,
				selection->standard_world,
				!selection->no_scenery, 0, archive, mesh, name,
				&error);
		} else if (selection->model_path == NULL) {
			status = mh_car_mesh_load(selection->root,
				(unsigned int)selection->car, archive, mesh,
				name, &error);
		} else {
			status = load_model_file(
				selection->model_path, archive, mesh, &error);
		}
	}

	mh_image_collection_close(archive);
	free(archive_file.data);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Model/texture loading: %s\n", error.message);
		return 0;
	}

	if ((!selection->world &&
		(selection->model_path == NULL ||
			selection->track_selected)) ||
		(selection->world && !selection->no_scenery)) {
		status = mh_mesh_environment_load(selection->root,
			(unsigned int)selection->track, mesh, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Environment loading: %s\n",
				error.message);
			mh_mesh_free(mesh);
			return 0;
		}
	}

	return 1;
}

static const char *
mesh_label(const struct model_selection *selection)
{
	if (selection->world) {
		if (selection->standard_world)
			return "standard world geometry";

		return "hardware world geometry";
	}

	if (selection->model_path == NULL)
		return "body + four wheels, reset pose";

	return "single model";
}

int
main(int argc, char *argv[])
{
	struct model_selection selection;
	struct mh_mesh mesh;
	enum parse_result parsed;
	struct mh_error error;
	enum mh_error_code status;
	char name[MESH_NAME_BYTES];
	int result;

	parsed = read_options(argc, argv, &selection);

	if (parsed != PARSE_READY)
		return parsed == PARSE_HELP ? 0 : 1;

	if (!load_mesh(&selection, &mesh, name))
		return 1;

	printf("%s: %lu triangles, %lu image slots (%s)\n",
		selection.model_path == NULL ? name : selection.model_path,
		(unsigned long)(mesh.vertex_count / 3),
		(unsigned long)mesh.texture_count, mesh_label(&selection));

	if (selection.check) {
		status = mh_mesh_animate(
			&mesh, selection.view.animation_seconds, &error);

		if (status != MH_ERROR_NONE)
			fprintf(stderr, "Animation: %s\n", error.message);

		mh_mesh_free(&mesh);

		return status == MH_ERROR_NONE ? 0 : 1;
	}

	if (selection.view.capture_path == NULL) {
		puts("Drag/arrows: orbit; wheel: zoom; E: reflections; R: reset camera; Esc: quit.");
		puts("Space: pause/resume scenery; Home: restart scenery at zero seconds.");
	}

	result = mh_model_viewer_run(
		&mesh, "Motorhead model viewer", &selection.view);
	mh_mesh_free(&mesh);

	if (result && selection.view.capture_path != NULL)
		printf("Wrote %s\n", selection.view.capture_path);

	return result ? 0 : 1;
}
