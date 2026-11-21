#ifndef MH_ASSETS_PREPARE_DEMO_AUDIO_H
#define MH_ASSETS_PREPARE_DEMO_AUDIO_H

#include "engine/core/error.h"

struct mh_resource_source;
struct mh_user_data;
struct mh_demo_recipe;

/*
 * First software demo binds all car crashes to its own majcrash.wav. Prepare
 * byte-identical aliases for the shared engine's two light-crash filenames.
 * Other packages already provide those files. Never overwrite a destination;
 * source and destination providers share the extractor's private staging tree.
 */
enum mh_error_code	mh_demo_audio_prepare(
	const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out);

#endif
