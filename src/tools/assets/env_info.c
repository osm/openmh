#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/env_sound.h"
#include "engine/audio/sound_spatial.h"
#include "engine/core/error.h"

#define LISTENER_COORD_LIMIT 1e6

int
main(int argc, char *argv[])
{
	struct mh_env_sounds sounds;
	struct mh_file file;
	struct mh_sound_listener listener;
	const struct mh_env_sound *sound;
	struct mh_error error;
	enum mh_error_code status;
	char *end;
	double eye[3], target[3], gains[2];
	unsigned int i;

	if (argc != 2 && argc != 5) {
		fprintf(stderr,
			"Usage: mh-env-info FILE.esd [X Y Z] (listener faces +Z)\n");
		return 1;
	}

	for (i = 0; i < 3; i++) {
		eye[i] = 0;

		if (argc == 5) {
			errno = 0;
			eye[i] = strtod(argv[2 + i], &end);

			if (errno != 0 || *end != 0 || end == argv[2 + i] ||
				!(eye[i] >= -LISTENER_COORD_LIMIT &&
					eye[i] <= LISTENER_COORD_LIMIT))
				return 1;
		}

		target[i] = eye[i];
	}

	target[2] += 1;
	mh_sound_listener_camera(eye, target, &listener);
	status = mh_file_read(argv[1], &file, &error);

	if (status == MH_ERROR_NONE) {
		status = mh_env_sounds_esd_decode(
			file.data, file.size, &sounds, &error);
		free(file.data);
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	for (i = 0; i < sounds.count; i++) {
		sound = &sounds.sounds[i];
		gains[0] = gains[1] = 0;

		if (sound->kind == MH_ENV_FIXED)
			status = mh_sound_spatial(&listener, sound->position,
				sound->volume, sound->near_distance,
				sound->far_distance, gains, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "%s\n", error.message);
			return 1;
		}

		printf("%u\t%s\t%u\t%u\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\n",
			(unsigned int)sound->kind, sound->sample,
			sound->object_class, sound->priority, sound->volume,
			sound->pitch, sound->near_distance, sound->far_distance,
			sound->position[0], sound->position[1],
			sound->position[2], gains[0], gains[1]);
	}

	return fflush(stdout) != 0;
}
