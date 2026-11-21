#ifndef MH_SERVER_VEHICLE_H
#define MH_SERVER_VEHICLE_H

#include "engine/network/history.h"

/* Remote snapshot receipt and dedicated pose/speed state, without physics. */
struct mh_server_vehicle {
	struct mh_net_history	history;
	unsigned int		view_player;
	unsigned long		top_speed_kmh;
	double			position[3], rotation[9], velocity[3];
};

void	mh_server_vehicle_reset(struct mh_server_vehicle *vehicle);

/*
 * The caller authenticates the peer and selects its racing slot. Accepts only
 * detailed type-13 updates, as in the original server dispatcher; compact
 * type-14 updates travel from server to client. The viewed slot changes only
 * for a newer snapshot.
 * Every valid update refreshes receipt time, including old/duplicate stamps.
 * Native validation and timestamp acceptance are shared with client history.
 * Failure preserves vehicle and accepted.
 */
enum mh_error_code	mh_server_vehicle_receive(
	struct mh_server_vehicle *vehicle, const struct mh_net_message *message,
	double now_ms, int *accepted, struct mh_error *error_out);

/*
 * Copy the latest received position, without client interpolation. Once ten
 * snapshots exist, reconstruct orientation and average nine interval velocities
 * to update maximum absolute forward speed. Called after transport service,
 * before course progress. No snapshots leaves the state unchanged.
 * Failure preserves vehicle. Position/velocity/matrix stores retain binary32.
 */
enum mh_error_code	mh_server_vehicle_sample(
	struct mh_server_vehicle *vehicle, struct mh_error *error_out);

#endif
