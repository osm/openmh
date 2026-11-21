#ifndef MH_CLIENT_SESSION_LOBBY_H
#define MH_CLIENT_SESSION_LOBBY_H

#include "client/application/activity.h"
#include "client/diagnostics/options.h"
#include "engine/core/error.h"
#include "engine/network/network_session.h"
#include "engine/ui/lobby_screen.h"

struct mh_horn_source;

struct mh_catalog_source;
struct mh_font_source_provider;
struct mh_image_decoder_provider;

struct mh_resource_source;
struct mh_user_data;

struct mh_network_lobby;
struct mh_game_timer;
struct mh_window_system;
struct mh_window;
struct mh_canvas_provider;
struct mh_input_provider;
struct mh_clipboard_provider;
struct mh_image_capture;
struct mh_image_encoder;

enum mh_network_lobby_result {
	MH_LOBBY_LEAVE,
	MH_LOBBY_RACE,
	MH_LOBBY_RECONNECT_SESSION,
	MH_LOBBY_QUIT
};

/*
 * Own original backdrop/font and catalog data; preserve this object through
 * lobby/race transitions. Close the active activity before releasing this
 * object.
 *
 * Timer, window system, canvas and input providers are borrowed through close;
 * the timer is shared with its races. A non-NULL window is borrowed through
 * close and must have no canvas/renderer attached when activity begins.
 *
 * Clipboard is optional (NULL means unavailable) and borrowed through close.
 * Diagnostic image capture is also optional and borrowed through close. Active
 * reads close with the activity. Optional screenshot storage, directory and
 * asset-root strings are borrowed through close. The image decoder is borrowed
 * through close for later statistics artwork.
 *
 * Catalog/horn sources, resources and font source are call-only; decoded asset
 * data is copied. Native conveniences are in
 * client/diagnostics/runtime_lobby.h.
 */
enum mh_error_code	mh_network_lobby_open_from(
	const struct mh_horn_source *horn_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *asset_root,
	int host, struct mh_game_timer *timer,
	const struct mh_window_system *windows, struct mh_window *window,
	const struct mh_canvas_provider *canvases,
	const struct mh_input_provider *inputs,
	const struct mh_clipboard_provider *clipboard,
	const struct mh_image_capture *capture,
	const struct mh_user_data *screenshot_data,
	const struct mh_image_encoder *screenshot_encoder,
	const char *screenshot_directory, const struct mh_debug_options *debug,
	struct mh_network_lobby **output, struct mh_error *error_out);

/* Load the patched client's last-race backdrop. Resources are copied. */
enum mh_error_code	mh_network_lobby_statistics_from(
	struct mh_network_lobby *lobby,
	const struct mh_resource_source *resources, const char *asset_root,
	struct mh_error *error_out);

/* Select the next lobby window's presentation before beginning its activity. */
enum mh_error_code	mh_network_lobby_fullscreen(
	struct mh_network_lobby *lobby, int fullscreen,
	struct mh_error *error_out);

/* Refresh after race settings edits, before starting another lobby activity. */
enum mh_error_code	mh_network_lobby_units(struct mh_network_lobby *lobby,
	unsigned int system, struct mh_error *error_out);

/*
 * Publish the frontend's course prefix. Keeps the full command catalog for
 * failed-lookup behavior, limits /track and F1 selection. The diagnostic
 * launcher retains the default full catalog. Atomic.
 */
enum mh_error_code	mh_network_lobby_track_limit(
	struct mh_network_lobby *lobby, unsigned int count,
	struct mh_error *error_out);
void			mh_network_lobby_close(struct mh_network_lobby *lobby);

/*
 * One lobby frame per step; all arguments remain live until activity close.
 * Output must be initialized and empty; only one activity may own this lobby.
 */
enum mh_error_code	mh_network_lobby_begin(struct mh_network_lobby *lobby,
	struct mh_network *network, unsigned long admission_seconds,
	enum mh_network_lobby_result *result,
	enum mh_error_code (*service)( void *context, struct mh_error *error_out),
	void *context, struct mh_activity *output, struct mh_error *error_out);

/* Blocking runners use the clock source borrowed by this lobby. */
enum mh_error_code	mh_network_lobby_run(struct mh_network_lobby *lobby,
	struct mh_network *network, unsigned long admission_seconds,
	enum mh_network_lobby_result *result, struct mh_error *error_out);

/*
 * Optional main-thread service, once per lobby iteration (e.g. inherited
 * menu music). A window close returns QUIT, Escape/console leave LEAVE.
 */
enum mh_error_code	mh_network_lobby_run_service(
	struct mh_network_lobby *lobby, struct mh_network *network,
	unsigned long admission_seconds, enum mh_network_lobby_result *result,
	enum mh_error_code (*service)( void *context, struct mh_error *error_out),
	void *context, struct mh_error *error_out);

/*
 * Import the race's complete display history; editable history is already
 * shared on the network connection. Reconnect starts a new receive revision.
 */
void	mh_network_lobby_history(struct mh_network_lobby *lobby,
	const struct mh_messages *messages, unsigned long chat_revision);

/*
 * Race entry clears display rows, retaining revision/color metadata and the
 * viewport-dependent wrap width already selected by the race.
 */
void	mh_network_lobby_begin_race(struct mh_network_lobby *lobby,
	struct mh_messages *messages);
void	mh_network_lobby_reconnected(struct mh_network_lobby *lobby);

/*
 * Borrow the original /record command path for the next race. NULL lobby
 * (terminal launcher) has no interactive recording request.
 */
const char	*mh_network_lobby_recording(
	const struct mh_network_lobby *lobby);

#endif
