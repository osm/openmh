#ifndef MH_ASSETS_MYO_DECODE_H
#define MH_ASSETS_MYO_DECODE_H

#include "engine/assets/myo.h"
#include "interfaces/geometry/model.h"

/*
 * Codec helper for an already validated MYO or MYW model view. Copies all
 * geometry and names, retaining no input bytes. Output unchanged on failure.
 * Raw views remain an original-format codec detail, not a consumer contract.
 */
enum mh_error_code	mh_model_myo_expand(const struct mh_myo *source,
	struct mh_model *model, struct mh_error *error_out);

#endif
