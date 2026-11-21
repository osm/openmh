#ifndef MH_CLIENT_SESSION_RECORDS_H
#define MH_CLIENT_SESSION_RECORDS_H

#include "engine/core/error.h"
#include "engine/game/highscore_awards.h"

struct mh_race_context;
struct mh_field;
struct mh_user_data;

struct mh_race_scores {
	struct mh_highscore_history	local_history;

	/* Borrowed session history; NULL selects local_history. */
	struct mh_highscore_history	*history;

	/* One exit/restart boundary, retained for retries. */
	struct mh_highscore_awards	awards;
	unsigned int			mode, difficulty, record_class;
	char				player_name[MH_HIGHSCORE_NAME_SIZE];
	int				bound;
};

/*
 * Bind through a prepared writable view borrowed until context close.
 * NULL directory selects its default root; an empty key is its logical root.
 * The context copies the chosen key. Errors leave the binding/field unchanged.
 */
enum mh_error_code	mh_race_records_open_from(
	const struct mh_user_data *data, struct mh_race_context *context,
	struct mh_field *field, const char *directory,
	struct mh_error *error_out);

/*
 * Bind a fresh race identity. The optional zero-initialized history outlives
 * all races in this menu/CLI session; its previous total is retained.
 * Record storage remains separately opt-in through records_open_from.
 */
enum mh_error_code	mh_race_scores_bind(struct mh_race_context *context,
	unsigned int mode, unsigned int difficulty, unsigned int record_class,
	const char *player_name, struct mh_highscore_history *history,
	struct mh_error *error_out);
enum mh_error_code	mh_race_scores_frame(struct mh_race_context *context,
	const struct mh_field *field, struct mh_error *error_out);

/*
 * CL/CR first, then original MHI awards. Repeated acceptance/cleanup saves
 * share one boundary. Errors retain the pending award for retry.
 * Uses the view bound at open; publication is separate from host COMMIT.
 */
enum mh_error_code	mh_race_records_save(struct mh_race_context *context,
	const struct mh_field *field, struct mh_error *error_out);

/*
 * Only after save succeeds AND a local grid reset succeeds. Retains the
 * original completion/total history. Recovery and lobby return do not call it.
 */
void	mh_race_records_restarted(struct mh_race_context *context);

#endif
