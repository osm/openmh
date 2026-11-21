#ifndef MH_NET_SESSION_H
#define MH_NET_SESSION_H

#include "engine/core/error.h"
#include "engine/input/lobby_keys.h"
#include "engine/network/car_catalog.h"
#include "engine/network/history.h"
#include "engine/network/lobby.h"
#include "engine/network/lobby_commands.h"
#include "engine/network/protocol_profile.h"
#include "engine/network/start.h"

#define MH_NET_CONNECTIONS 16
/*
 * Six simultaneous lobby updates plus a start can address seven peers:
 * 49 original packets must fit one atomic controller operation.
 */
#define MH_NET_OUTBOX 64
/* Largest lobby frame: patched roster. */
#define MH_NET_SESSION_PACKET_BYTES 1103
#define MH_NET_CHAT_LINES 8
#define MH_NET_ANNOUNCEMENTS 16
#define MH_NET_ANNOUNCEMENT_BYTES 80

enum mh_net_session_phase {
	MH_NET_CONNECTING,
	MH_NET_JOINING,
	MH_NET_LOBBY,
	MH_NET_REJECTED,
	MH_NET_CLOSED,
	MH_NET_LOADING,
	MH_NET_WAITING,
	MH_NET_RACING
};

struct mh_net_session_config {
	/*
	 * The host chooses Hz; joining clients adopt it. Zero selects legacy
	 * variable-frame timing, incompatible with fixed-tick sessions.
	 */
	unsigned int			simulation_rate;

	/* Zero retains the existing F1.2 format. */
	enum mh_net_wire		wire;
	unsigned int			provider;
	char	password[MH_NET_PASSWORD_BYTES], track[MH_NET_NAME_BYTES];
	unsigned int			mode, laps, car_count;

	/* Dedicated native RNG, original generator/selection. */
	unsigned long			grid_seed;
	struct mh_net_profile		profile;
	struct mh_net_prediction	prediction;
};

struct mh_net_outgoing {
	unsigned long	peer;

	/* Disconnect after delivery; size zero means disconnect only. */
	int		reliable, disconnect;
	size_t		size;
	unsigned char	data[MH_NET_SESSION_PACKET_BYTES];
};

struct mh_net_connection {
	struct mh_net_challenge	challenge;
	unsigned long		peer, ping_ms;
	int			closing, ping_pending, race_ping;
	double			connected_ms, contact_ms, ping_sent_ms;
	unsigned long		ping_stamp;
};

struct mh_net_chat_line {
	unsigned int	player;
	unsigned long	color;
	char		text[MH_NET_CHAT_BYTES];
};

struct mh_net_replication {
	int			configured, local_valid;
	double			view_distance;
	struct mh_net_vehicle	local;
	struct mh_net_history	history[MH_NET_WIRE_PLAYERS];

	/* Current presentation pose; initial grid before warmup. */
	double			position[MH_NET_WIRE_PLAYERS][3];
	unsigned int		view[MH_NET_WIRE_PLAYERS];
	unsigned long		sent_ms[MH_NET_WIRE_PLAYERS], status_ms;
	unsigned int		pending_status;

	/*
	 * Patched host status and legacy ping/pong service wait for the send
	 * deadline. OpenMH acknowledgements belong to the transport channel.
	 */
	struct mh_net_batch	pending_service[MH_NET_WIRE_PLAYERS];

	/* Presentation slots with a profile/status sample. */
	unsigned int		status_valid;

	/* Host mode-1 mask; shares status bit 8 with staleness. */
	unsigned int		eliminated;

	/*
	 * Lobby presentation receives type 15 without configuring cars.
	 * Patched ping persists across inactive slots and replication resets.
	 */
	struct mh_net_status	status[MH_NET_WIRE_PLAYERS];
	unsigned long		status_revision;
};

struct mh_net_announcement {
	/* Wire-slot mask; only current race members. */
	unsigned int	recipients;

	/* 0 targeted notice (type 8), 1 broadcast log text (type 7). */
	int		chat;
	unsigned long	color;
	char		text[MH_NET_ANNOUNCEMENT_BYTES];
};

struct mh_net_progress {
	unsigned int	position, lap;
};

struct mh_net_session {
	const struct mh_net_protocol	*protocol;
	int				host;
	enum mh_net_session_phase	phase;
	unsigned int			player;
	double				now_ms, contact_ms;
	struct mh_net_session_config	config;

	/* Owned host catalog; zero for protocol-only fixtures. */
	struct mh_net_car_catalog	cars;
	struct mh_net_lobby		lobby;
	struct mh_net_roster		roster;
	struct mh_net_connection	connections[MH_NET_CONNECTIONS];
	struct mh_net_outgoing		outgoing[MH_NET_OUTBOX];
	unsigned int			out_head, out_count;

	/*
	 * Ordered display text, including original lobby system notifications.
	 * Client acceptance can append before its first roster is received.
	 */
	struct mh_net_chat_line		chat[MH_NET_CHAT_LINES];
	unsigned int			chat_count;
	unsigned long			roster_revision, chat_revision;

	/* Host type 8, distinct from chat history. */
	struct mh_net_chat_line		notice;
	unsigned long			notice_revision;

	/*
	 * Last dedicated result survives race loading and return to the lobby.
	 */
	struct mh_net_statistics	last_race;
	unsigned long			statistics_revision;
	unsigned int			rejection_reason;

	/*
	 * Transport denial text is complete; application replies use a prefix.
	 */
	int				transport_rejection;
	char				rejection[MH_NET_TEXT_LIMIT + 1];
	struct mh_net_start		start;

	/* Received type 16; game frame supplies its receipt clock. */
	int				start_pending;
	unsigned int			ready_mask;

	/* Host state: waiting for returning peers to make contact. */
	unsigned int			return_pending;
	unsigned long			grid_seed;

	/*
	 * Patched send timing survives race loading; lobby status has its own
	 * clock.
	 */
	unsigned long			race_service_ms;
	int				kicked;

	/*
	 * Patched host progress survives return/removal, separate from
	 * profiles.
	 */
	struct mh_net_progress		progress[MH_NET_PLAYERS];
	struct mh_net_replication	replication;
};

/*
 * Portable lobby/race session, including mixed participation. Late clients
 * join the lobby until the next start; transport supplies peer IDs/time.
 * Init, receive, connected, disconnected, tick and user operations are
 * atomic on error, including the bounded outbox. Packet batches are fully
 * validated before state changes. Main thread only. Replication is enabled
 * explicitly after the caller resolves the frozen roster's race assets.
 * Dedicated type-9 receipt selects ready drivers from the hidden-server
 * roster. Passing/uncommitted profiles remain in the lobby in the native
 * frozen field, rather than becoming transient cars until server status.
 */
enum mh_error_code	mh_net_session_init(int host,
	const struct mh_net_session_config *config, struct mh_net_session *out,
	struct mh_error *error_out);

/*
 * Addressable lobby slots, including slot zero. Patched clients can receive
 * a dedicated roster; listen hosts and F1.2 retain eight slots. Before
 * acceptance, player equals this capacity (it is not a valid slot).
 */
unsigned int	mh_net_session_capacity(const struct mh_net_session *session);

/*
 * Bind installed sorted car names before accepting any peers. Required by
 * playable host adapters; codec-only fixtures may omit asset resolution.
 * Only initial type-4 joins use the fallback, also while a race is running.
 */
enum mh_error_code	mh_net_session_cars(struct mh_net_session *session,
	const char *const *names, unsigned int count,
	struct mh_error *error_out);
enum mh_error_code	mh_net_session_connected_from(
	struct mh_net_session *session, unsigned long peer, double now_ms,
	const struct mh_net_challenge *challenge, struct mh_error *error_out);
enum mh_error_code	mh_net_session_connected(struct mh_net_session *session,
	unsigned long peer, double now_ms, struct mh_error *error_out);
enum mh_error_code	mh_net_session_disconnected(
	struct mh_net_session *session, unsigned long peer, double now_ms,
	struct mh_error *error_out);
enum mh_error_code	mh_net_session_receive(struct mh_net_session *session,
	unsigned long peer, const unsigned char *data, size_t size,
	int reliable, double now_ms, struct mh_error *error_out);
enum mh_error_code	mh_net_session_tick(struct mh_net_session *session,
	double now_ms, struct mh_error *error_out);

/*
 * Run the start barrier once inside a sampled race frame, after clock/list
 * sampling and before physics. Transport tick/receive do not advance it.
 * Host uses current ready flags/RTTs; clients consume a pending type 16.
 * Clock is the extended game milliseconds, independent of transport time.
 * WAITING/RACING only. Queued signals and clock/phase changes are atomic.
 */
enum mh_error_code	mh_net_session_start_frame(
	struct mh_net_session *session, long double game_ms,
	struct mh_error *error_out);
enum mh_error_code	mh_net_session_profile(struct mh_net_session *session,
	const struct mh_net_profile *profile, struct mh_error *error_out);
enum mh_error_code	mh_net_session_chat(struct mh_net_session *session,
	const char *text, unsigned long color, struct mh_error *error_out);

/*
 * Execute a lobby command against the current native session. Initialize
 * console to zero once per connection; confirmation and demo path persist
 * there, while player/setup fields refresh from the session. Host UI owns
 * score-clear confirmation; clients cannot arm that prompt. Session, console
 * and output commit together, including bounded queues.
 *
 * Profile/setup/password/score/kick/chat/quit effects are applied here. LOCAL
 * notices remain in output for the full presentation history. SEND contributes
 * a local session chat echo only with a paired LOCAL event; do not also insert
 * that event. Patched server commands send without echo. PALETTE and RECONNECT
 * require presentation/transport handling by the caller; recording retains its
 * requested path in console. Output preserves all original actions for tracing,
 * including applied ones.
 */
enum mh_error_code	mh_net_session_command(struct mh_net_session *session,
	struct mh_lobby_command_state *console,
	const struct mh_lobby_command_catalog *catalog, const char *text,
	struct mh_lobby_command_output *output, struct mh_error *error_out);

/*
 * Toggle confirmed dedicated-lobby readiness. Passing requires two ordered
 * /ready commands; queue them atomically. Roster flags remain server-owned.
 */
enum mh_error_code	mh_net_session_toggle_ready(
	struct mh_net_session *session, struct mh_error *error_out);

/*
 * F-key/paste controller bound to an admitted lobby session, or a
 * rejected/closed client retaining its local selections before reconnect.
 * Detached clients update config and timers without touching roster slots or
 * queuing PROFILE actions to the old connection. Returned actions still
 * describe the controller's requests; admission publishes retained choices.
 *
 * Zero controls on screen entry; retain its timers, draft and confirmation
 * between frames. Session values refresh before input; edits update locally at
 * once, while PROFILE/ROSTER actions send their exact intermediate values.
 * Share confirm_clear with the command console before/after submissions.
 *
 * CONFIRM/WAIT notices remain in output for the presentation history. START
 * freezes the race through begin_race, including its admission guard. Session,
 * controls and output roll back together if validation/enqueue fails. Clipboard
 * retrieval, network polling and later Escape are caller-owned.
 */
enum mh_error_code	mh_net_session_keys(struct mh_net_session *session,
	struct mh_lobby_keys *controls,
	const struct mh_lobby_key_catalog *catalog,
	const struct mh_lobby_key_input *input, double now_ms,
	struct mh_lobby_key_output *output, struct mh_error *error_out);
enum mh_error_code	mh_net_session_leave(struct mh_net_session *session,
	struct mh_error *error_out);

/*
 * Host freezes the current lobby roster and sends original type 9.
 * Returning lobby members must acknowledge first. F1.2 also waits for pending
 * admissions; patched setup selects admitted lobby members only.
 * Peers resolve/validate assets and
 * build their race before calling loaded; this API does no asset I/O.
 * Once loaded, OpenMH clients reliably notify the host. Legacy clients
 * satisfy the host ready barrier with matching race pong replies.
 * The race caller then supplies its sampled game clock to start_frame.
 */
enum mh_error_code	mh_net_session_begin_race(
	struct mh_net_session *session, double now_ms,
	struct mh_error *error_out);
enum mh_error_code	mh_net_session_loaded(struct mh_net_session *session,
	double now_ms, struct mh_error *error_out);

/*
 * Call during LOADING with original track ViewDistance and active grid display
 * positions indexed through roster.player_count. Resets histories, send clocks,
 * view targets and status. Output packets remain original types 13/14/15;
 * native transport is separate.
 */
enum mh_error_code	mh_net_session_configure_replication(
	struct mh_net_session *session, double view_distance,
	const double position[][3], struct mh_error *error_out);

/*
 * Supply the latest local capture and viewed slot during loading/wait/race.
 * Snapshot stamps use session transport time at send. Host relays the newest
 * received snapshot with its original sender stamp and original re-quantizer.
 */
enum mh_error_code	mh_net_session_publish(struct mh_net_session *session,
	const struct mh_net_vehicle *vehicle, unsigned int viewed_player,
	struct mh_error *error_out);

/*
 * Sample one remote; updates its host distance-selection presentation pose.
 * Original ten-packet warmup, interpolation, stale behavior and bounds apply.
 */
enum mh_error_code	mh_net_session_sample(struct mh_net_session *session,
	unsigned int player, double now_ms, struct mh_net_playback *output,
	struct mh_error *error_out);

/*
 * Host-owned status bytes: classified position and lap. Separate from
 * profile/grid values. Byte-width API preserves the wire; the race bridge
 * supplies field values.
 */
enum mh_error_code	mh_net_session_status(struct mh_net_session *session,
	unsigned int player, unsigned int value_a, unsigned int value_b,
	struct mh_error *error_out);

/*
 * Type 18 returns to lobby without disconnecting: host returns the field;
 * a client returns individually while other racers continue. Atomic.
 */
enum mh_error_code	mh_net_session_return_lobby(
	struct mh_net_session *session, struct mh_error *error_out);

/*
 * Normal/elimination completion: score every still-active slot using host
 * positions, then send type 18 and the updated type 6 roster. Atomic, including
 * queues; repeating after the phase transition fails without awarding twice.
 */
enum mh_error_code	mh_net_session_finish_race(
	struct mh_net_session *session,
	const unsigned int positions[MH_NET_PLAYERS],
	struct mh_error *error_out);

/*
 * Up to sixteen short race announcements. Pack original messages in reliable
 * per-recipient batches, apply local notices/chat in order, commit atomically.
 * This bounds simultaneous elimination broadcasts without enlarging outboxes.
 */
enum mh_error_code	mh_net_session_announcements(
	struct mh_net_session *session,
	const struct mh_net_announcement *events, unsigned int count,
	struct mh_error *error_out);

/*
 * Original targeted host type 8 notification. Local target updates the same
 * presentation state directly. Does not append to chat history.
 */
enum mh_error_code	mh_net_session_notice(struct mh_net_session *session,
	unsigned int player, const char *text, unsigned long color,
	struct mh_error *error_out);

/* Host-only lobby removal, original type 19 followed by transport close. */
enum mh_error_code		 mh_net_session_kick(
	struct mh_net_session *session, unsigned int player,
	struct mh_error *error_out);
const struct mh_net_outgoing	*mh_net_session_outgoing(
	const struct mh_net_session *session);
void				 mh_net_session_sent(
	struct mh_net_session *session);

/*
 * Valid OpenMH traffic updates liveness independently of snapshots/readiness.
 */
enum mh_error_code	mh_net_session_contact(struct mh_net_session *session,
	unsigned long peer, double now_ms, unsigned long rtt_ms, int sampled,
	struct mh_error *error_out);

#endif
