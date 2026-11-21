#ifndef MH_ASSETS_DEMO_CAR_H
#define MH_ASSETS_DEMO_CAR_H

#include "engine/assets/demo_definition.h"
#include "engine/core/file.h"
#include "interfaces/catalog/car.h"

/* Decode the original TBL05 binary record to the ordinary catalog value. */
enum mh_error_code	mh_car_demo_decode(const struct mh_file *source,
	struct mh_car *output, struct mh_error *error_out);

/*
 * Shared bounded normalization for the decoder and openmotorex's text-file
 * writer.
 */
enum mh_error_code	mh_car_demo_definition(const struct mh_file *source,
	struct mh_definition_text *output, struct mh_error *error_out);

#endif
