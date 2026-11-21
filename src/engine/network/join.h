#ifndef MH_NET_JOIN_H
#define MH_NET_JOIN_H

#include "engine/network/protocol.h"

/*
 * Common listen/dedicated admission checks: provider, version, password, in
 * that order. ACCEPT means credentials passed; the caller must still reserve
 * a slot and set player, or reject a full lobby. No slot or clock state here.
 * OpenMH UDP verifies a case-sensitive proof. Legacy joins use bounded ASCII
 * folding. Callers retain their wire
 * validation and diagnostics for malformed requests before these checks.
 * Errors preserve reply.
 */
enum mh_error_code	mh_net_join_check(unsigned int provider,
	const char *password, const struct mh_net_join *request,
	struct mh_net_message *reply, struct mh_error *error_out);

/* Native hosts require the fixed-timing version before advertising a rate. */
enum mh_error_code	mh_net_join_check_timing(unsigned int provider,
	const char *password, unsigned int simulation_rate,
	const struct mh_net_join *request, struct mh_net_message *reply,
	struct mh_error *error_out);

#endif
