#include <string.h>

#include "engine/audio/sound_pool.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

/* Native bound before original per-ear saturation and score preparation. */
#define PREPARED_GAIN_LIMIT 1e6

static int
boolean_valid(int value)
{
	return value == 0 || value == 1;
}

static int
request_valid(const struct mh_sound_request *request)
{
	return request != NULL && request->flags <= MH_U32_MAX &&
		request->priority <= MH_U32_MAX &&
		boolean_valid(request->ready) &&
		boolean_valid(request->eligible) &&
		request->left >= -PREPARED_GAIN_LIMIT &&
		request->left <= PREPARED_GAIN_LIMIT &&
		request->right >= -PREPARED_GAIN_LIMIT &&
		request->right <= PREPARED_GAIN_LIMIT;
}

static int
pool_valid(const struct mh_sound_pool *pool)
{
	unsigned int i;
	const struct mh_sound_record *record;

	if (pool == NULL || pool->count == 0 ||
		pool->count > MH_SOUND_RECORDS || pool->channel_count == 0 ||
		pool->channel_count > MH_SOUND_CHANNELS_MAX)
		return 0;

	/*
	 * Copying a shared definition can leave aliases to an inactive record,
	 * or several channels pointing at a record whose channel field names
	 * only one of them. These are reachable original states, not ownership
	 * errors.
	 */
	for (i = 0; i < pool->channel_count; i++)
		if (pool->channels[i] > pool->count)
			return 0;

	for (i = 0; i < MH_SOUND_PENDING; i++)
		if (pool->pending[i] > pool->count)
			return 0;

	for (i = 0; i < pool->count; i++) {
		record = &pool->records[i];

		if (!request_valid(&record->request) || record->active > 1 ||
			record->channel > pool->channel_count ||
			!(record->score >= 0 &&
				record->score <= (double)MH_U32_MODULUS))
			return 0;
	}

	return 1;
}

enum mh_error_code
mh_sound_pool_init(unsigned int count, struct mh_sound_pool *pool,
	struct mh_error *error_out)
{
	return mh_sound_pool_init_channels(
		count, MH_SOUND_CHANNELS, pool, error_out);
}

enum mh_error_code
mh_sound_pool_init_channels(unsigned int count, unsigned int channels,
	struct mh_sound_pool *pool, struct mh_error *error_out)
{
	if (pool == NULL || count == 0 || count > MH_SOUND_RECORDS ||
		channels == 0 || channels > MH_SOUND_CHANNELS_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid sound pool size");

	memset(pool, 0, sizeof(*pool));
	pool->count = count;
	pool->channel_count = channels;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sound_pool_configure(struct mh_sound_pool *pool, unsigned int id,
	const struct mh_sound_request *request, struct mh_error *error_out)
{
	if (!pool_valid(pool) || id == 0 || id > pool->count ||
		!request_valid(request))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sound record configuration");

	pool->records[id - 1].request = *request;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sound_pool_replace(struct mh_sound_pool *pool, unsigned int id,
	const struct mh_sound_request *request, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_sound_pool_configure(pool, id, request, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	pool->records[id - 1].active = pool->records[id - 1].channel = 0;
	pool->records[id - 1].score = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
stop_record(struct mh_sound_pool *pool, unsigned int id)
{
	struct mh_sound_record *record;
	unsigned int i, channel;

	if (id == 0)
		return 0;

	record = &pool->records[id - 1];

	if (!record->active)
		return 0;

	channel = record->channel;

	if (channel != 0)
		pool->channels[channel - 1] = 0;

	for (i = 0; i < MH_SOUND_PENDING; i++)
		if (pool->pending[i] == id)
			pool->pending[i] = 0;

	record->active = record->channel = 0;

	return channel;
}

enum mh_error_code
mh_sound_pool_stop(struct mh_sound_pool *pool, unsigned int id,
	unsigned int *stopped, struct mh_error *error_out)
{
	if (!pool_valid(pool) || id > pool->count || stopped == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid sound stop");

	*stopped = stop_record(pool, id);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sound_pool_request(struct mh_sound_pool *pool, unsigned int id,
	unsigned int *stopped, struct mh_error *error_out)
{
	struct mh_sound_record *record;
	unsigned int i;

	if (!pool_valid(pool) || id == 0 || id > pool->count || stopped == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid sound request");

	*stopped = 0;
	record = &pool->records[id - 1];

	if (!record->request.ready)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (record->active) {
		if (!(record->request.flags & MH_SOUND_RETRIGGER))
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		*stopped = stop_record(pool, id);
	}

	for (i = 0; i < MH_SOUND_PENDING; i++) {
		if (pool->pending[i] == id)
			break;

		if (pool->pending[i] == 0) {
			pool->pending[i] = id;
			record->active = 1;
			break;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sound_pool_clear(struct mh_sound_pool *pool, unsigned int *stopped_mask,
	struct mh_error *error_out)
{
	unsigned int i, mask, channel;

	if (!pool_valid(pool) || stopped_mask == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid sound clear");

	mask = 0;

	for (i = 0; i < pool->channel_count; i++)
		if (pool->channels[i] != 0) {
			channel = stop_record(pool, pool->channels[i]);

			if (channel != 0)
				mask |= 1U << (channel - 1);
		}

	for (i = 0; i < MH_SOUND_PENDING; i++) {
		channel = stop_record(pool, pool->pending[i]);

		if (channel != 0)
			mask |= 1U << (channel - 1);
	}

	*stopped_mask = mask;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static float
clamp_gain(float gain)
{
	return gain < 0 ? 0 : gain > 1 ? 1 : gain;
}

static int
prepare_score(struct mh_sound_record *record)
{
	float left, right;

	if (!record->request.eligible)
		return 0;

	left = clamp_gain(record->request.left);
	right = clamp_gain(record->request.right);
	record->score = (float)((double)record->request.priority +
		(left > right ? left : right) * .5);

	return 1;
}

static unsigned int
select_channel(
	const struct mh_sound_pool *pool, const struct mh_sound_record *record)
{
	unsigned int i, minimum;

	minimum = 0;

	for (i = 0; i < MH_SOUND_CHANNELS; i++) {
		if (pool->channels[i] == 0)
			return i + 1;

		if (pool->records[pool->channels[i] - 1].score <
			pool->records[pool->channels[minimum] - 1].score)
			minimum = i;
	}

	return record->score > pool->records[pool->channels[minimum] - 1].score
		? minimum + 1
		: 0;
}

static void
append_change(struct mh_sound_changes *changes, enum mh_sound_change_kind kind,
	unsigned int record, unsigned int channel)
{
	struct mh_sound_change *entry;

	entry = &changes->entries[changes->count++];
	entry->kind = kind;
	entry->record = record;
	entry->channel = channel;
}

static enum mh_error_code
refresh_request(struct mh_sound_record *record, unsigned int id,
	mh_sound_prepare_fn prepare, void *context, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (prepare == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = prepare(context, id, &record->request, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!request_valid(&record->request))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid prepared sound request");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
admit_pending(struct mh_sound_pool *pool, unsigned int *playing_mask,
	mh_sound_prepare_fn prepare, void *context,
	struct mh_sound_changes *changes, struct mh_error *error_out)
{
	struct mh_sound_record *record;
	unsigned int i, id, channel, previous, stopped;
	enum mh_error_code status;

	/* Pending requests compete before refreshing resident scores. */
	for (i = 0; i < MH_SOUND_PENDING; i++) {
		id = pool->pending[i];

		if (id == 0)
			continue;

		record = &pool->records[id - 1];

		status = refresh_request(
			record, id, prepare, context, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		channel = prepare_score(record) ? select_channel(pool, record)
						: 0;

		if (channel != 0) {
			previous = pool->channels[channel - 1];

			if (previous != 0) {
				stopped = stop_record(pool, previous);

				if (stopped != 0) {
					append_change(changes, MH_SOUND_STOP,
						previous, stopped);
					*playing_mask &= ~(1U << (stopped - 1));
				}
			}

			record->channel = channel;
			pool->channels[channel - 1] = id;
			*playing_mask |= 1U << (channel - 1);
			append_change(changes, MH_SOUND_START, id, channel);
		} else
			record->active = 0;

		pool->pending[i] = 0;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
refresh_residents(struct mh_sound_pool *pool, unsigned int playing_mask,
	mh_sound_prepare_fn prepare, void *context,
	struct mh_sound_changes *changes, struct mh_error *error_out)
{
	struct mh_sound_record *record;
	unsigned int i, id, channel, stopped;
	enum mh_error_code status;
	int eligible;

	for (i = 0; i < MH_SOUND_CHANNELS; i++) {
		id = pool->channels[i];

		if (id == 0)
			continue;

		record = &pool->records[id - 1];
		channel = record->channel;

		if (channel == 0)
			continue;

		eligible = (playing_mask & (1U << (channel - 1))) != 0;

		if (eligible) {
			status = refresh_request(
				record, id, prepare, context, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			eligible = prepare_score(record);
		}

		if (!eligible) {
			stopped = stop_record(pool, id);

			if (stopped != 0) {
				append_change(
					changes, MH_SOUND_STOP, id, stopped);
				playing_mask &= ~(1U << (stopped - 1));
			}
		} else
			append_change(changes, MH_SOUND_ADJUST, id, channel);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
schedule_update(struct mh_sound_pool *pool, unsigned int playing_mask,
	mh_sound_prepare_fn prepare, void *context,
	struct mh_sound_changes *changes, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (!pool_valid(pool) || pool->channel_count != MH_SOUND_CHANNELS ||
		changes == NULL || playing_mask > MH_SOUND_CHANNEL_MASK)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sound pool update");

	memset(changes, 0, sizeof(*changes));
	status = admit_pending(
		pool, &playing_mask, prepare, context, changes, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return refresh_residents(
		pool, playing_mask, prepare, context, changes, error_out);
}

enum mh_error_code
mh_sound_pool_update(struct mh_sound_pool *pool, unsigned int playing_mask,
	struct mh_sound_changes *changes, struct mh_error *error_out)
{
	return schedule_update(
		pool, playing_mask, NULL, NULL, changes, error_out);
}

enum mh_error_code
mh_sound_pool_update_prepared(struct mh_sound_pool *pool,
	unsigned int playing_mask, mh_sound_prepare_fn prepare, void *context,
	struct mh_sound_changes *changes, struct mh_error *error_out)
{
	struct mh_sound_pool next;
	struct mh_sound_changes result;
	enum mh_error_code status;

	if (pool == NULL || changes == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing prepared sound update");

	next = *pool;
	status = schedule_update(
		&next, playing_mask, prepare, context, &result, error_out);

	if (status == MH_ERROR_NONE) {
		*pool = next;
		*changes = result;
	}

	return status;
}
