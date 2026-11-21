#ifndef MH_AUDIO_SURFACE_H
#define MH_AUDIO_SURFACE_H

#include "engine/audio/crash.h"
#include "engine/audio/mixer.h"
#include "engine/core/error.h"
#include "engine/simulation/field.h"

#define MH_SURFACE_FIXED_SAMPLES 9
#define MH_SURFACE_HISTORY_VOICES 6
#define MH_SURFACE_SAMPLES                                                     \
	(MH_MATERIAL_LIMIT * MH_MATERIAL_SOUNDS + MH_SURFACE_FIXED_SAMPLES +   \
		MH_CRASH_CLASSES)

/* Logical records in the surface bank, not physical output channels. */
enum mh_surface_voice {
	MH_SURFACE_VOICE_CONTACT = 0,
	MH_SURFACE_VOICE_SPIN = 1,
	MH_SURFACE_VOICE_SLIDE = 2,
	MH_SURFACE_VOICE_GLOBAL_SLIDE = 3,
	MH_SURFACE_VOICE_RELEASE_FIRST = 4,
	MH_SURFACE_VOICE_RELEASE_SECOND = 5,
	MH_SURFACE_VOICE_BODY_WEAK = 6,
	MH_SURFACE_VOICE_BODY_SCRATCH = 7,
	MH_SURFACE_VOICE_REMOTE_FIRST = 8,
	MH_SURFACE_VOICE_BODY_HARD = 15,
	MH_SURFACE_VOICE_CRASH = 16,
	MH_SURFACE_VOICE_LOCAL_SKID = 17,
	MH_SURFACE_VOICE_BURST_FIRST = 18,
	MH_SURFACE_VOICE_BURST_SECOND = 19,
	MH_SURFACE_VOICE_EXTRA_REMOTE = 20
};

/* Fixed prefix of the decoded sample bank; shared with the asset loader. */
enum mh_surface_sample {
	MH_SURFACE_SAMPLE_SLIDE = 0,
	MH_SURFACE_SAMPLE_RELEASE_FIRST = 1,
	MH_SURFACE_SAMPLE_RELEASE_SECOND = 2,
	MH_SURFACE_SAMPLE_SPIN = 3,
	MH_SURFACE_SAMPLE_BARREL = 4,
	MH_SURFACE_SAMPLE_CRASH = 5,
	MH_SURFACE_SAMPLE_MAJOR_CRASH = 6,
	MH_SURFACE_SAMPLE_BURST_FIRST = 7,
	MH_SURFACE_SAMPLE_BURST_SECOND = 8
};

/*
 * Fixed samples: Slide1, SlideE1, SlideE2, Spin, Barrel, Crash2, MajCrash,
 * EBurst1/2. Engine bursts share this bank's skid/crash selection counter.
 */
struct mh_surface_bank {
	struct mh_materials	materials;
	struct mh_crash_bank	crashes;
	unsigned int		sample[MH_MATERIAL_LIMIT][MH_MATERIAL_SOUNDS];

	/* PCM bytes are borrowed from the surface audio asset owner. */
	struct mh_audio_pcm	waves[MH_SURFACE_SAMPLES];
	unsigned int		count;
};

enum mh_surface_slide {
	MH_SURFACE_SLIDE_OFF = 0,
	MH_SURFACE_SLIDE_MATERIAL = 1,
	MH_SURFACE_SLIDE_GLOBAL = 2
};

struct mh_surface_parameters {
	int	material, spin;
	int	slide; /* mh_surface_slide */
	double	contact_gain, spin_gain, spin_pitch, slide_gain, remote_gain;
	int	remote_slide;
};

struct mh_body_sound_parameters {
	int	material, weak, hard, scratch;
	double	weak_gain, hard_gain, scratch_gain, pan;
};

struct mh_surface_sound {
	struct mh_mixer		mixer;
	struct mh_crash_sound	crash;

	/* Per-car tyre material history. */
	int			material[MH_RACE_MAX_CARS];

	/* Shared contact, slide and spin sample bindings. */
	int			bound[3];
	unsigned long		random;

	/* Per-car body material history, retained on view changes. */
	int			body_material[MH_RACE_MAX_CARS];

	/* Shared weak, scratch and hard bindings. */
	int			body_bound[3];
	unsigned long		listener_revision;
};

/*
 * The six shared material copies survive device/race teardown. The next setup
 * disables their samples but retains activity and channel assignments. No
 * sample pointers or asset-relative bindings cross this boundary.
 */
struct mh_surface_history {
	/* Contact, slide, spin, weak, scratch and hard, in that order. */
	unsigned int	active[MH_SURFACE_HISTORY_VOICES];
	unsigned int	channel[MH_SURFACE_HISTORY_VOICES];
};

unsigned int	mh_surface_skid_voice(unsigned int entry);

/*
 * Original selection and stored binary32 gains, before device saturation.
 * Native wheel IDs >25 follow the existing grip fallback to material zero.
 * Remote gain may be negative after snapshot extrapolation; the device clamps.
 * No state or output changes on invalid inputs.
 */
enum mh_error_code	mh_surface_parameters(
	const struct mh_materials *materials,
	const struct mh_vehicle_state *vehicle, int listener_mode,
	struct mh_surface_parameters *output, struct mh_error *error_out);

/*
 * Original offsets are 16-bit sample positions, not byte positions or an end
 * index. Native bank requires mono 16-bit PCM for these original sounds.
 * Clamps, swaps reversed endpoints, and restores the whole sample if equal.
 */
enum mh_error_code	mh_surface_loop(const struct mh_audio_pcm *wave,
	unsigned long start, unsigned long tail, size_t *loop_start,
	size_t *loop_end, struct mh_error *error_out);

enum mh_error_code	mh_surface_history_save(
	const struct mh_surface_sound *sound,
	struct mh_surface_history *history, struct mh_error *error_out);

/*
 * Restore into a freshly initialized, pool-bound bank. Old channel numbers
 * do not recreate channel-table entries or physical playback.
 */
enum mh_error_code	mh_surface_history_restore(
	struct mh_surface_sound *sound,
	const struct mh_surface_history *history, struct mh_error *error_out);

/*
 * Voice slots: contact 0, spin 1, material slide 2, Slide1 3, releases 4/5,
 * weak body/scratch 6/7, remote skids 8..14, hard body 15, selected car/object
 * crash 16, positional local skid 17, engine braking bursts 18/19.
 * Bank memory must outlive playback.
 */
enum mh_error_code	mh_surface_sound_init(unsigned long rate, double gain,
	struct mh_surface_sound *output, struct mh_error *error_out);

/* Bank-only diagnostic pass; vehicle_sound.h supplies cross-bank order. */
enum mh_error_code	mh_surface_sound_update(struct mh_surface_sound *sound,
	const struct mh_surface_bank *bank, const struct mh_field *field,
	int listener_mode, int paused, struct mh_error *error_out);

#endif
