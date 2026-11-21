#ifndef MH_CATALOG_PROFILE_RUNTIME_H
#define MH_CATALOG_PROFILE_RUNTIME_H

#include "engine/core/error.h"
#include "interfaces/catalog/profile.h"

struct mh_horn_catalog;

/*
 * First fill (count+1)*0x1b00 portrait bytes with two rand calls per byte.
 * Preserve that RNG prefix before initializing any ADP, even though rendering
 * loads its pictures separately. Zero count draws nothing. The catalog bound
 * applies; invalid input preserves the RNG.
 */
enum mh_error_code	mh_profile_catalog_advance_rng(unsigned int count,
	unsigned long *random, struct mh_error *error_out);

/*
 * Initialize the runtime horn from a validated decoded profile. Consume the
 * constructor's initial random horn draw even when the profile overrides it,
 * then apply the
 * original name lookup/reseed fallback. Store a canonical horn display name.
 * Presence bits still describe source fields. Vehicle names remain unresolved.
 * Caller supplies the shared RNG and invokes once per profile construction.
 * The definition owns its inline fields and has already been validated by its
 * loader. Output may alias definition. Both output and RNG remain unchanged
 * on failure; neither input nor horn catalog is retained.
 */
enum mh_error_code	mh_profile_initialize(
	const struct mh_profile *definition,
	const struct mh_horn_catalog *horns, unsigned long *random,
	struct mh_profile *output, struct mh_error *error_out);

#endif
