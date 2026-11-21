#ifndef MH_AUDIO_SOUND_POOL_H
#define MH_AUDIO_SOUND_POOL_H

#include "engine/core/error.h"
#define MH_SOUND_CHANNELS 8
/* Spatial queues own separate admission policies: DS3D 16, EAX 24. */
#define MH_SOUND_CHANNELS_MAX 24
#define MH_SOUND_PENDING 32
/* Six native banks, including all menu horns. */
#define MH_SOUND_RECORDS 270
/* At most one stop/start per request and one change per resident. */
#define MH_SOUND_CHANGES (MH_SOUND_PENDING * 2 + MH_SOUND_CHANNELS)
#define MH_SOUND_CHANNEL_MASK ((1U << MH_SOUND_CHANNELS) - 1U)

/* Known record bits; other original bits remain valid and are preserved. */
enum mh_sound_flags {
	MH_SOUND_LOOP = 1,
	MH_SOUND_POSITIONAL = 2,
	MH_SOUND_RETRIGGER = 4
};

/*
 * Sound pool policy. IDs and channels are 1-based; zero denotes an empty slot.
 * The record bound is implementation-defined; queue/channel limits preserve
 * compatibility. The default initializer selects eight channels; alternate
 * consumers may use up to CHANNELS_MAX for producer stop/clear bookkeeping. The
 * software update entry points require eight. No sample ownership, PCM,
 * platform or clock types.
 */
struct mh_sound_request {
	unsigned long	flags, priority; /* Original unsigned 32-bit fields. */

	/* Prepared ear gains, before [0,1] saturation. */
	float		left, right;

	/* Record/sample loaded and nonempty, checked on requests. */
	int		ready;

	/* Result of positional/range parameter preparation. */
	int		eligible;
};

typedef enum mh_error_code (*mh_sound_prepare_fn)(void *context,
	unsigned int id, struct mh_sound_request *request,
	struct mh_error *error_out);

struct mh_sound_record {
	struct mh_sound_request	request;
	unsigned int		active, channel;

	/* Cached f32(priority + max(clamped ears)*.5). */
	float			score;
};

struct mh_sound_pool {
	struct mh_sound_record	records[MH_SOUND_RECORDS];
	unsigned int	pending[MH_SOUND_PENDING], channels[MH_SOUND_CHANNELS_MAX];
	unsigned int		count, channel_count;
};

enum mh_sound_change_kind {
	MH_SOUND_STOP,
	MH_SOUND_START,
	MH_SOUND_ADJUST
};

struct mh_sound_change {
	enum mh_sound_change_kind	kind;
	unsigned int			record, channel;
};

struct mh_sound_changes {
	struct mh_sound_change	entries[MH_SOUND_CHANGES];
	unsigned int		count;
};

/*
 * All errors preserve state and outputs. Configure changes parameters only;
 * active/cached score are refreshed by the original scheduling stages.
 */
enum mh_error_code	mh_sound_pool_init(unsigned int count,
	struct mh_sound_pool *pool, struct mh_error *error_out);

/* Alternate consumers retain the producer queue, but own channel admission. */
enum mh_error_code	mh_sound_pool_init_channels(unsigned int count,
	unsigned int channels, struct mh_sound_pool *pool,
	struct mh_error *error_out);
enum mh_error_code	mh_sound_pool_configure(struct mh_sound_pool *pool,
	unsigned int id, const struct mh_sound_request *request,
	struct mh_error *error_out);

/*
 * Copy an inactive definition over a shared record. Reset
 * activity/channel/score without stopping PCM or removing existing pointers.
 * Old slots may alias this record; its current channel is authoritative.
 */
enum mh_error_code	mh_sound_pool_replace(struct mh_sound_pool *pool,
	unsigned int id, const struct mh_sound_request *request,
	struct mh_error *error_out);

/*
 * Active requests are ignored unless MH_SOUND_RETRIGGER permits retrigger. Full
 * pending queues silently decline a request. A retrigger can stop a channel
 * even if the new request is then declined. stopped receives that channel,
 * or zero. The backend must apply this stop before the next update.
 */
enum mh_error_code	mh_sound_pool_request(struct mh_sound_pool *pool,
	unsigned int id, unsigned int *stopped, struct mh_error *error_out);
enum mh_error_code	mh_sound_pool_stop(struct mh_sound_pool *pool,
	unsigned int id, unsigned int *stopped, struct mh_error *error_out);

/*
 * Visit channels/requests and stop active records. Stale references to inactive
 * records survive. Preserve parameters/cached scores. stopped_mask identifies
 * channels to stop (bit 0 is channel 1).
 */
enum mh_error_code	mh_sound_pool_clear(struct mh_sound_pool *pool,
	unsigned int *stopped_mask, struct mh_error *error_out);

/*
 * playing_mask is the backend's current channel status. Pending requests
 * compete against cached scores BEFORE resident completion/gain refresh.
 * Starts become live for this update. Apply emitted backend changes in order.
 * Callers serialize access; the pool does not synchronize internally.
 */
enum mh_error_code	mh_sound_pool_update(struct mh_sound_pool *pool,
	unsigned int playing_mask, struct mh_sound_changes *changes,
	struct mh_error *error_out);

/*
 * Prepare at the original pending/resident stages, without refreshing cached
 * resident scores early. The callback may change only its request and owned
 * scratch context. Errors preserve pool/changes; context is not rolled back.
 */
enum mh_error_code	mh_sound_pool_update_prepared(
	struct mh_sound_pool *pool, unsigned int playing_mask,
	mh_sound_prepare_fn prepare, void *context,
	struct mh_sound_changes *changes, struct mh_error *error_out);

#endif
