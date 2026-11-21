#ifndef MH_CLIENT_APPLICATION_H
#define MH_CLIENT_APPLICATION_H

#include "client/application/status.h"
#include "client/menus/configuration.h"
#include "engine/core/error.h"
#include "engine/settings/av.h"

enum {
	MH_APPLICATION_PATH_SIZE = 1024
};

struct mh_retro_source;
struct mh_horn_source;

struct mh_env_sound_source;
struct mh_material_source;
struct mh_catalog_source;
struct mh_profile_source;
struct mh_palette_source;
struct mh_menu_layout_source;
struct mh_navigation_source;
struct mh_animation_source;
struct mh_atlas_source;

struct mh_model_decoder;
struct mh_world_decoder;
struct mh_collision_source;

struct mh_application;
struct mh_app_lifecycle;
struct mh_storage;
struct mh_clock_source;
struct mh_resource_source;
struct mh_resource_archive_provider;
struct mh_user_data;
struct mh_image_capture;
struct mh_window_system;

struct mh_image_source_provider;
struct mh_font_source_provider;
struct mh_wireframe_source;
struct mh_sprite_source_provider;

/* Selected host services; the descriptor itself is copied during open. */
struct mh_app_services {
	/* Default installation root, copied during open. NULL selects ".". */
	const char					*game_directory;

	/*
	 * Selected asset sources and artwork/portrait decoders; borrowed
	 * through application close, including later race loads and progress
	 * awards.
	 */
	const struct mh_image_source_provider		*image_source;
	const struct mh_font_source_provider		*font_source;
	const struct mh_wireframe_source		*wireframe_source;
	const struct mh_sprite_source_provider		*sprite_source;
	const struct mh_model_decoder			*model_decoder;
	const struct mh_world_decoder			*world_decoder;
	const struct mh_atlas_source			*atlas_source;
	const struct mh_animation_source		*animation_source;
	const struct mh_palette_source			*palette_source;
	const struct mh_menu_layout_source		*layout_source;
	const struct mh_retro_source			*retro_source;
	const struct mh_profile_source			*profile_source;
	const struct mh_catalog_source			*catalog_source;
	const struct mh_material_source			*material_source;
	const struct mh_horn_source			*horn_source;
	const struct mh_env_sound_source		*env_sound_source;
	const struct mh_navigation_source		*navigation_source;
	const struct mh_collision_source		*collision_source;
	const struct mh_image_decoder_provider		*image_decoder;
	const struct mh_image_decoder_provider		*thumbnail_decoder;
	const struct mh_image_decoder_provider		*portrait_decoder;

	/* Optional host quiescence/recovery; copied during open. */
	const struct mh_app_lifecycle			*lifecycle;

	/* Compatible window/scene/canvas selection, copied during open. */
	const struct mh_render_service			*rendering;

	/* Optional screenshot storage; usually the same view as user_data. */
	const struct mh_user_data			*screenshot_data;

	/* Input provider borrowed through child and device teardown. */
	const struct mh_input_provider			*inputs;

	/* Optional encoded-audio decoder, borrowed through media teardown. */
	const struct mh_audio_decoder_provider		*audio_decoder;

	/* Optional output/scheduler, borrowed through audio teardown. */
	const struct mh_audio_provider			*audio;
	unsigned int					audio_trace;

	/* Optional video provider; borrowed through child teardown. */
	const struct mh_video_provider			*videos;

	/* Optional transport, borrowed through discovery and session close. */
	const struct mh_transport_provider		*network;

	/* Optional original-peer transport; NULL falls back to network. */
	const struct mh_transport_provider		*legacy_network;
	int						network_trace;

	/* Optional clipboard capability; borrowed through child teardown. */
	const struct mh_clipboard_provider		*clipboard;

	/*
	 * Optional encoders indexed by mh_screenshot_format, borrowed through
	 * teardown.
	 */
	const struct mh_image_encoder
		*screenshot_encoders[MH_SCREENSHOT_FORMAT_COUNT];

	/* Borrowed until close; NULL selects userdata. */
	const char					*screenshot_directory;
	const struct mh_user_data			*user_data;
	const struct mh_resource_source			*resources;

	/* Optional archive mounting selected by the host. */
	const struct mh_resource_archive_provider	*resource_archives;
	const struct mh_clock_source			*clock;
	struct mh_storage				*storage;
};

/*
 * The host owns the application until close; argv and its strings must remain
 * live throughout. Only one application may own the process history. Open
 * leaves output unchanged on failure. Help and command-line diagnostics are
 * terminal applications whose native exit code is returned by close.
 *
 * The application owns a game timer using the supplied clock. Clock
 * callbacks/context remain borrowed until close, as do the resource and
 * writable-data sources and their callbacks. Window and input providers are
 * also borrowed until close; the application releases only its own
 * window-system references, after closing dependent windows. The host keeps
 * both storage views ready before PREPARE completes.
 *
 * League catalog initialization/creation queries the clock's optional local
 * calendar, including during menu startup. A host serving those menus supplies
 * date; its absence reports UNSUPPORTED without changing game timing.
 *
 * Storage transfers only on success. Failed open leaves output and service
 * lifetimes with the caller. Optional captures and screenshot storage are
 * selected independently; NULL leaves those features unavailable.
 */
enum mh_error_code	mh_application_open_with_services(int argc, char **argv,
	const struct mh_app_services *services, struct mh_application **output,
	struct mh_error *error_out);

/*
 * A storage failure can be retried without replaying the suspended application
 * step or its local transactions. Error text remains valid until retry/close.
 */
enum mh_error_code	mh_application_storage_error(
	const struct mh_application *application, struct mh_error *error_out);
enum mh_error_code	mh_application_retry_storage(
	struct mh_application *application, struct mh_error *error_out);

/*
 * Host-thread transitions between steps. Missing host lifecycle support returns
 * UNSUPPORTED. Repeated requests are no-ops. Suspension retains the current
 * screen/session, stops application frames and excludes background time from
 * game/input timers. step may still poll pending storage while suspended.
 * The host continues delivering lifecycle notifications independently of the
 * game's input queue and owns scheduling; a zero delay is not a busy-wait hint.
 * Closing while suspended releases resources without resuming first.
 */
enum mh_error_code	mh_application_set_suspended(struct mh_application *app,
	int suspended, struct mh_error *error_out);
int			mh_application_suspended(
	const struct mh_application *app);

/*
 * Save idle-menu preferences/controls/league changes and request durable
 * COMMIT. During a live session only already-produced storage mutations are
 * committed; this does not save an in-progress race. STATE means
 * PREPARE/another commit is pending, or the app is terminal: retry only after
 * the pending step completes. Success starts a commit; step polls it even while
 * suspended. Local save errors return directly; commit failures use the
 * existing storage error/retry path. Request checkpoints before OS deadlines;
 * forced termination need not provide time for one last commit.
 */
enum mh_error_code	mh_application_checkpoint(struct mh_application *app,
	struct mh_error *error_out);

/*
 * Optional host restrictions, before the first step. Content and missing
 * services may restrict these further; they can never enable a missing feature.
 */
enum mh_error_code	mh_application_menu_restrict(struct mh_application *app,
	enum mh_menu_feature feature, enum mh_menu_availability state,
	struct mh_error *error_out);

/* Advance startup, a menu frame or the active session without sleeping. */
enum mh_app_status	mh_application_step(struct mh_application *application);
unsigned long		mh_application_delay(
	const struct mh_application *application);

/*
 * Close releases ownership. A running application is cancelled without final
 * menu saves. Normal completion finishes saves and their storage commit during
 * step, before DONE; close performs only diagnostics and resource cleanup.
 * Close consumes the pointer and returns the native exit code (0 on success).
 */
int	mh_application_close(struct mh_application *application);

#endif
