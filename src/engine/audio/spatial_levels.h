#ifndef MH_SPATIAL_LEVELS_H
#define MH_SPATIAL_LEVELS_H

struct mh_clock_source;

/*
 * Shared EAX/DS3D arithmetic. Inputs are finite, gain nonnegative, frequency
 * positive, counts/stamps u32. History must not exceed clock time. Borrow a
 * working millisecond clock below MH_CLOCK_MS_LIMIT. Each operation samples the
 * clock separately as the original does; zero-length fades do not.
 */
unsigned long	mh_spatial_timestamp(const struct mh_clock_source *clock);
float		mh_spatial_fade_in(unsigned long started, unsigned long samples,
	float frequency, const struct mh_clock_source *clock);
float		mh_spatial_fade_out(unsigned long fading, unsigned long samples,
	float frequency, const struct mh_clock_source *clock);
float		mh_spatial_frequency(float frequency);
long		mh_spatial_volume(float gain, float attack, float release,
	float *log_gain);

/* Expiry uses raw frequency; fade factors use the clamped frequency. */
int	mh_spatial_release_finished(unsigned long *fading, int loop,
	int looping, unsigned long samples, float frequency,
	const struct mh_clock_source *clock);

#endif
