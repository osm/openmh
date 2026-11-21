#ifndef MH_RACE_PROFILE_INTERNAL_H
#define MH_RACE_PROFILE_INTERNAL_H

#include "engine/audio/race_effects.h"

struct mh_audio;
struct mh_race_profile;

/*
 * Race-owned composition of the selected effects profile and sample banks.
 * Success attaches to audio. Its sample/name storage, clock and optional world
 * are borrowed and must outlive the profile owner. Results may release this
 * owner after detaching its device callbacks.
 */
enum mh_error_code	mh_race_profile_create(struct mh_audio *audio,
	int selection, double gain, const struct mh_world *world,
	struct mh_error *error_out);
void			mh_race_profile_destroy(
	struct mh_race_profile *profile);

enum mh_error_code	mh_race_profile_clear(struct mh_race_profile *profile,
	struct mh_error *error_out);
enum mh_error_code	mh_race_profile_finish(struct mh_race_profile *profile,
	struct mh_error *error_out);
void			mh_race_profile_stop_device(
	struct mh_race_profile *profile);
enum mh_error_code	mh_race_profile_stops(struct mh_race_profile *profile,
	struct mh_error *error_out);

void	mh_race_profile_listener(struct mh_race_profile *profile,
	const struct mh_sound_listener *listener, float reverb);
void	mh_race_profile_gain(struct mh_race_profile *profile, double gain);
void	mh_race_profile_reset_clock(struct mh_race_profile *profile);

unsigned int	mh_race_profile_active_voices(
	const struct mh_race_profile *profile);
void		mh_race_profile_buffer_stats(
	const struct mh_race_profile *profile,
	struct mh_race_effect_buffer_stats *output);
unsigned int	mh_race_profile_capacity(int selection);

enum mh_error_code	mh_race_profile_update(struct mh_race_profile *profile,
	struct mh_error *error_out);
enum mh_error_code	mh_race_profile_service_buffer(
	struct mh_race_profile *profile, struct mh_error *error_out);
enum mh_error_code	mh_race_profile_render(struct mh_race_profile *profile,
	float *samples, size_t frames, int paused, struct mh_error *error_out);

#endif
