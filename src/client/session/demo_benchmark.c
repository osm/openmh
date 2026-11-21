#include <float.h>
#include <stdio.h>
#include <string.h>

#include "client/session/demo_benchmark.h"

enum {
	REPORT_TEXT_CAPACITY = 4096,
	RECORDING_NAME_LIMIT = 1023
};

static const char *
on_off(unsigned int enabled)
{
	return enabled ? "On" : "Off";
}

static const char *
choice(const char *const *names, unsigned int count, unsigned int value)
{
	return value < count ? names[value] : "Unknown";
}

static char *
write_settings(char *text, const struct mh_av_settings *settings)
{
	static const char *const info[] = {"None", "Selective", "All"};
	static const char *const detail[] = {
		"Low", "Medium", "High", "Max", "Custom"};

	static const char *const name_plates[] = {"Off", "Flat", "Transparent"};
	static const char *const shading[] = {
		"Flat", "Gouraud", "Reflection", "Glenz"};

	if (settings == NULL)
		return text;

	text += sprintf(text, "Info detail:\t\t\t%s\nInfo map:\t\t\t%s\n",
		choice(info, 3, settings->info_detail),
		choice(info, 3, settings->road_map));
	text += sprintf(text, "Graphic detail:\t\t\t%s\n",
		choice(detail, 5, settings->detail_mode));
	text += sprintf(text, "Lensflares:\t\t\t%s\nSparks:\t\t\t\t%s\n",
		on_off(settings->lens_flares), on_off(settings->sparks));
	text += sprintf(text, "Smoke:\t\t\t\t%s\nHalos:\t\t\t\t%s\n",
		on_off(settings->smoke), on_off(settings->halos));
	text += sprintf(text, "Skidmarks:\t\t\t%s\nShadow:\t\t\t\t%s\n",
		on_off(settings->skid_marks), on_off(settings->shadow));
	text += sprintf(text, "Nameplates:\t\t\t%s\nBackground:\t\t\t%s\n",
		choice(name_plates, 3, settings->name_plates),
		settings->sky_image ? "Picture" : "Blank");
	text += sprintf(text, "Track detail:\t\t\t%s\nCar detail:\t\t\t%s\n",
		choice(detail + 1, 2, settings->track_detail),
		choice(detail, 3, settings->car_detail));
	text += sprintf(text,
		"Car shading:\t\t\t%s\nView distance:\t\t\t%03.1f%%\n",
		choice(shading, 4, settings->car_shading),
		settings->view_distance);
	text += sprintf(
		text, "Motionblur:\t\t\t%s\n", on_off(settings->motion_blur));

	return text;
}

enum mh_error_code
mh_demo_benchmark_save(const struct mh_user_data *data,
	const struct mh_clock_source *clock,
	const struct mh_demo_benchmark *benchmark, const char *recording,
	const struct mh_race_context *context, int muted,
	struct mh_error *error_out)
{
	/* Bounded recording name, fixed labels and at most three doubles. */
	char report[REPORT_TEXT_CAPACITY + 3 * DBL_MAX_10_EXP], *text;
	struct mh_clock_date date;
	double seconds, fps;
	enum mh_error_code status;

	if (data == NULL || clock == NULL || recording == NULL ||
		strlen(recording) > RECORDING_NAME_LIMIT || context == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid benchmark report request");

	status = mh_demo_benchmark_result(benchmark, &seconds, &fps, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_clock_source_date(clock, &date, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	text = report;
	text += sprintf(text,
		"**************************************************************\n");
	text += sprintf(text,
		"*** Motorhead Benchmark Results, %04u%02u%02u, %02u:%02u\n",
		date.year, date.month, date.day, date.hour, date.minute);
	text += sprintf(text,
		"*** Native port; fixed 30 ms playback, measured game time\n*** Camera: Track; native renderer settings below\n");
	text += sprintf(text, "*** Demo file: %s\n", recording);
	text += sprintf(text,
		"Frames/Sec:\t\t\t%03.1f\nTime:\t\t\t\t%03.1f\nFrames:\t\t\t\t%lu\n",
		fps, seconds, benchmark->frames);
	text += sprintf(text,
		"Render device:\t\t\t%s\nRequested audio:\t\t%s\n",
		context->scene.assets.profile == MH_RENDER_SOFTWARE
			? "Native software"
			: "Native Glide-style",
		on_off(!muted));
	text += sprintf(text, "Screen width:\t\t\t%u\nScreen height:\t\t\t%u\n",
		context->scene.assets.view_width,
		context->scene.assets.view_height);
	text = write_settings(text, context->settings);
	text += sprintf(text,
		"**************************************************************\n\n");
	status = mh_user_data_replace(
		data, "Fps.txt", report, (size_t)(text - report), error_out);

	if (status != MH_ERROR_NONE)
		return mh_error_set(error_out, status,
			"cannot write benchmark report Fps.txt");

	printf("Benchmark: %.1f frames/sec, %lu frames in %.3f seconds; saved Fps.txt\n",
		fps, benchmark->frames, seconds);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
