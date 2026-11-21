#ifndef MH_RACE_SOUND_H
#define MH_RACE_SOUND_H

#include "engine/audio/engine_sound.h"
#include "engine/audio/mixer.h"
#include "engine/core/error.h"
#include "engine/simulation/field.h"

/* Race-bank mixer slots, distinct from the sample indices below. */
enum mh_race_voice {
	MH_RACE_VOICE_ENGINE_BASE = 7, /* Opponents use base + field slot. */
	MH_RACE_VOICE_LOCAL_ENGINE = 15,
	MH_RACE_VOICE_GEAR = 17,
	MH_RACE_VOICE_FINISH = 18,
	MH_RACE_VOICE_CHECKPOINT = 19,
	MH_RACE_VOICE_HORN_BASE = 20,
	MH_RACE_VOICE_FINISH_COPY = 28,
	MH_RACE_VOICE_ELIMINATED = 29,
	MH_RACE_VOICE_ELIMINATED_COPY = 30,
	MH_RACE_VOICE_MESSAGE = 31,
	MH_RACE_VOICE_EXTRA_ENGINE = 35,
	MH_RACE_VOICE_EXTRA_HORN = 40
};

enum mh_race_sample {
	MH_SOUND_ENEMY = MH_ENGINE_SAMPLES,
	MH_SOUND_GEAR,
	MH_SOUND_COUNT,
	MH_SOUND_GO,
	MH_SOUND_CHECKPOINT,
	MH_SOUND_FINISH,
	MH_SOUND_ELIMINATED,
	MH_SOUND_MESSAGE,
	MH_RACE_SAMPLES
};

struct mh_horn_sound {
	struct mh_audio_pcm	wave;
	double			volume, pitch;
};

struct mh_sound_bank {
	/* PCM bytes are borrowed from the race audio asset owner. */
	struct mh_audio_pcm	waves[MH_RACE_SAMPLES];

	/*
	 * Content availability: one bit per explicitly absent optional sample.
	 */
	unsigned int		absent_samples;

	/* Field order, one bound CHF per car. */
	struct mh_horn_sound	horns[MH_RACE_MAX_CARS];
};

enum mh_countdown_voice {
	MH_COUNTDOWN_THREE = 16,
	MH_COUNTDOWN_TWO = 32,
	MH_COUNTDOWN_ONE = 33,
	MH_COUNTDOWN_GO = 34
};

#define MH_COUNTDOWN_CUES 4

struct mh_race_sound {
	struct mh_mixer	mixer;

	/* Per-car manual gear cue cache. */
	unsigned int	gear[MH_RACE_MAX_CARS];

	/* Ordered 3/2/1/GO. */
	int		countdown[MH_COUNTDOWN_CUES];
	unsigned long	notice_revision, listener_revision;
	int		finish_latch, checkpoint_latch;
};

/* Preserve the legacy voices; extra cars use separate logical records. */
unsigned int	mh_race_engine_voice(unsigned int entry);
unsigned int	mh_race_horn_voice(unsigned int entry);

enum mh_error_code	mh_race_sound_init(unsigned long rate, double gain,
	struct mh_race_sound *sound, struct mh_error *error_out);

/*
 * Vehicle sound phase, before physics/progress. pedal is the local driving
 * pedal (brake in automatic reverse); other detailed owners use their
 * sound_pedal. Per-car listener_mask bits select detailed layers/horns
 * independently of the viewed entry. Multiple owners update shared voices in
 * field order. The viewed camera selects cockpit mixing for every owner.
 *
 * Listener command revisions reset this mixer before rebuilding requests. Reset
 * state when starting a new race; paused calls neither trigger events nor
 * change layers.
 *
 * Horns use drive.horn, play once, ignore retriggers while active, and finish
 * after release. A held request repeats only after the sample has completed.
 * Manual gear cues update only the available detailed owner's cache. Listener
 * changes stop the shared voice but retain all per-car gear caches.
 *
 * Bank-only diagnostic pass; interactive races use vehicle_sound.h to
 * interleave surface/burst/body requests with each car.
 */
enum mh_error_code	mh_race_sound_vehicles(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	double pedal, int paused, struct mh_error *error_out);

/*
 * Preview the original viewed-car finish gate without changing state. The
 * race coordinator uses this to mute music and stop all other banks BEFORE
 * cues. No menu/info/intro gate applies to this shared latch.
 */
enum mh_error_code	mh_race_sound_finish_pending(
	const struct mh_race_sound *sound, const struct mh_field *field,
	int *pending, struct mh_error *error_out);

/*
 * Later HUD phase: countdown/finish/notice cues consume updated progress.
 * Countdown records have independent latches; menu/info/absent intro retain
 * them. An enabled pass clears each latch outside its open time interval.
 * Finish first stops this bank, requests both sounds and sets the shared
 * viewed-car latch; an unfinished viewed car clears it. NULL bank advances
 * latches without requesting PCM (missing native assets). The caller owns
 * other banks/music/listener flags. Pause preserves everything.
 */
enum mh_error_code	mh_race_sound_cues(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	int menu, int info, int paused, struct mh_error *error_out);

/*
 * Diagnostic combined update on one supplied snapshot. Both phases and
 * the optional shared pool are atomic on failure. Interactive frames use
 * vehicle_sound.h before physics and HUD cues afterwards.
 */
enum mh_error_code	mh_race_sound_update(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	double pedal, int paused, struct mh_error *error_out);

/*
 * Race presentation owns this separate cue: viewed held notice, display mode
 * 0/1/2 and duration gate its shared latch. Call after listener updates while
 * presenting the race. Disabled display and pause retain the latch; an enabled
 * empty notice clears it. Active playback is never restarted.
 */
enum mh_error_code	mh_race_sound_checkpoint(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	unsigned int gap_mode, int paused, struct mh_error *error_out);

/*
 * Request menu/cursor1.wav while newest-message age is <500ms. Flags zero
 * ignore active retriggers; a finished sample may repeat while the caller
 * continues requesting it. Logical voice 31, priority 3.
 */
enum mh_error_code	mh_race_sound_message(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, int requested, int paused,
	struct mh_error *error_out);

#endif
