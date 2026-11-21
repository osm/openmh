#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/settings/av.h"

/* Read one byte beyond the settings parser's 1 MiB limit. */
#define SETTINGS_READ_BYTES (1024UL * 1024UL + 1)

static void
print_settings(const struct mh_av_settings *settings, int apply_preset)
{
	static const char *const detail_names[] = {
		"Low", "Medium", "High", "Max", "Custom"};

	static const char *const info_names[] = {"None", "Selective", "All"};
	static const char *const shading_names[] = {
		"Flat", "Gouraud", "Reflection", "Glenz"};

	static const char *const plate_names[] = {
		"None", "Flat", "Transparent"};

	puts("Graphics/audio settings only; initial device volumes assumed 255/255.");
	printf("Screen: %lu x %lu; brightness ratios: %.9g %.9g %.9g\n",
		settings->width, settings->height,
		(double)settings->brightness[0],
		(double)settings->brightness[1],
		(double)settings->brightness[2]);
	printf("Info: %s; checkpoints: %s for %lu ms; map: %s\n",
		info_names[settings->info_detail],
		info_names[settings->checkpoint_info],
		settings->checkpoint_delay, info_names[settings->road_map]);
	printf("Detail mode: %s (%s)\n", detail_names[settings->detail_mode],
		apply_preset ? "preset applied" : "file values retained");
	printf("Lens flares: %u; sparks: %u; smoke: %u; halos: %u; skid marks: %u; shadow: %u\n",
		settings->lens_flares, settings->sparks, settings->smoke,
		settings->halos, settings->skid_marks, settings->shadow);
	printf("Name plates: %s; sky: %u; track detail: %s; car detail: %s\n",
		plate_names[settings->name_plates], settings->sky_image,
		detail_names[settings->track_detail + 1],
		detail_names[settings->car_detail]);
	printf("Car shading: %s; perspective: %s / %s; view distance: %.9g%%; motion blur: %u\n",
		shading_names[settings->car_shading],
		settings->perspective_on ? "All" : "Selective",
		settings->perspective == MH_AV_PERSPECTIVE_GOOD ? "Good"
								: "Normal",
		(double)settings->view_distance, settings->motion_blur);
	printf("Effects volume: %u/255; music volume: %u/255; CD loop: %s\n",
		settings->effects_volume, settings->music_volume,
		settings->cd_loop ? "On" : "Off");
}

int
main(int argc, char *argv[])
{
	struct mh_av_settings settings;
	unsigned char *text;
	const char *path;
	struct mh_error error;
	enum mh_error_code status;
	size_t size;
	FILE *file;
	int apply_preset, failed;

	apply_preset = argc == 3 && strcmp(argv[1], "--apply-preset") == 0;

	if (argc != 2 && !apply_preset) {
		fprintf(stderr,
			"Usage: mh-settings-info [--apply-preset] motorhead.cfg\n");
		return 1;
	}

	path = argv[apply_preset ? 2 : 1];
	file = fopen(path, "rb");

	if (file == NULL) {
		fprintf(stderr, "%s: cannot open settings\n", path);
		return 1;
	}

	text = (unsigned char *)malloc(SETTINGS_READ_BYTES);

	if (text == NULL) {
		fclose(file);
		fprintf(stderr, "out of memory reading settings\n");
		return 1;
	}

	size = fread(text, 1, SETTINGS_READ_BYTES, file);
	failed = ferror(file);

	if (fclose(file) != 0)
		failed = 1;

	status = failed
		? mh_error_set(&error, MH_ERROR_IO, "cannot read settings")
		: mh_av_settings_init(&settings, MH_AV_VOLUME_MAX,
			MH_AV_VOLUME_MAX, &error);

	if (status == MH_ERROR_NONE)
		status = mh_av_settings_parse(text, size, &settings, &error);

	if (status == MH_ERROR_NONE && apply_preset)
		status = mh_av_settings_preset(&settings, &error);

	free(text);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s: %s\n", path, error.message);
		return 1;
	}

	print_settings(&settings, apply_preset);

	return ferror(stdout) ? 1 : 0;
}
