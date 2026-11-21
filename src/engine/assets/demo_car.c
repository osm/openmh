#include <stdlib.h>
#include <string.h>

#include "engine/assets/car.h"
#include "engine/assets/demo_car.h"
#include "engine/assets/demo_definition.h"
#include "engine/assets/tbl.h"

#define CAR_RECORD_BYTES 0x13ac
#define HALO_OFFSET 0xe10
#define HALO_BYTES 1024

/*
 * Disk byte offsets, not native structure members.
 */
static const struct mh_definition_field fields[] = {
	{"CarName", 0x000, MH_DEFINITION_STRING, 1},
	{"Division", 0x100, MH_DEFINITION_INTEGER, 1},
	{"FileName", 0x104, MH_DEFINITION_STRING, 1},
	{"CollisionName", 0x304, MH_DEFINITION_STRING, 1},
	{"LineObjectName", 0x404, MH_DEFINITION_STRING, 1},
	{"Wheel1", 0x604, MH_DEFINITION_PAIR, 1},
	{"Wheel2", 0x804, MH_DEFINITION_PAIR, 1},
	{"Wheel3", 0xa04, MH_DEFINITION_PAIR, 1},
	{"Wheel4", 0xc04, MH_DEFINITION_PAIR, 1},
	{"Colour1", 0xe04, MH_DEFINITION_COLOR, 1},
	{"Colour2", 0xe08, MH_DEFINITION_COLOR, 1},
	{"Colour3", 0xe0c, MH_DEFINITION_COLOR, 1},
	{"Gears", 0x1210, MH_DEFINITION_INTEGER, 1},
	{"TopSpeed", 0x1214, MH_DEFINITION_FLOAT, 1},
	{"Acceleration", 0x1228, MH_DEFINITION_FLOAT, 1},
	{"Handling", 0x122c, MH_DEFINITION_FLOAT, 1},
	{"PhyWeight", 0x1238, MH_DEFINITION_FLOAT, 1},
	{"PhyGearRatio", 0x123c, MH_DEFINITION_FLOAT, 7},
	{"PhyMinRPM", 0x1258, MH_DEFINITION_FLOAT, 1},
	{"PhyMaxRPM", 0x125c, MH_DEFINITION_FLOAT, 1},
	{"PhyAccForce", 0x1260, MH_DEFINITION_FLOAT, 1},
	{"PhyBrakeForce", 0x1264, MH_DEFINITION_FLOAT, 1},
	{"PhyTurnForce", 0x1268, MH_DEFINITION_FLOAT, 1},
	{"PhyCenterMass", 0x126c, MH_DEFINITION_FLOAT, 3},
	{"PhyWheelFL", 0x1278, MH_DEFINITION_FLOAT, 3},
	{"PhyWheelFR", 0x1284, MH_DEFINITION_FLOAT, 3},
	{"PhyWheelBR", 0x1290, MH_DEFINITION_FLOAT, 3},
	{"PhyWheelBL", 0x129c, MH_DEFINITION_FLOAT, 3},
	{"PhyCornerFL", 0x12a8, MH_DEFINITION_FLOAT, 3},
	{"PhyCornerFR", 0x12b4, MH_DEFINITION_FLOAT, 3},
	{"PhyCornerBR", 0x12c0, MH_DEFINITION_FLOAT, 3},
	{"PhyCornerBL", 0x12cc, MH_DEFINITION_FLOAT, 3},
	{"PhySpringLength", 0x12d8, MH_DEFINITION_FLOAT, 1},
	{"PhySpringStrength", 0x12dc, MH_DEFINITION_FLOAT, 1},
	{"SpeedLevel", 0x12e0, MH_DEFINITION_INTEGER, 1},
	{"AccLevel", 0x12e4, MH_DEFINITION_INTEGER, 1},
	{"GripLevel", 0x12e8, MH_DEFINITION_INTEGER, 1},
	{"ShadowPoints1", 0x12ec, MH_DEFINITION_FLOAT, 12},
	{"ShadowPoints2", 0x131c, MH_DEFINITION_FLOAT, 12},
	{"ShadowPoints3", 0x134c, MH_DEFINITION_FLOAT, 12},
	{"ShadowPoints4", 0x137c, MH_DEFINITION_FLOAT, 12}};

static enum mh_error_code
read_record(const unsigned char *record, size_t size,
	struct mh_definition_text *output, struct mh_error *error_out)
{
	struct mh_definition_text text;
	size_t i;
	enum mh_error_code status;

	if (size != CAR_RECORD_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unexpected demo car record size");

	memset(&text, 0, sizeof(text));

	for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		status = mh_definition_write(
			&text, record, &fields[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_definition_paths(
		&text, record + HALO_OFFSET, HALO_BYTES, "Halo", error_out);

	if (status == MH_ERROR_NONE)
		*output = text;

	return status;
}

enum mh_error_code
mh_car_demo_definition(const struct mh_file *source,
	struct mh_definition_text *output, struct mh_error *error_out)
{
	unsigned char *record;
	size_t size;
	enum mh_error_code status;

	if (source == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing demo car preparation input/output");

	if (source->size != CAR_RECORD_BYTES + 4)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unexpected wrapped demo car record size");

	status = mh_tbl_decode(
		source->data, source->size, &record, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = read_record(record, size, output, error_out);
	free(record);

	return status;
}

enum mh_error_code
mh_car_demo_decode(const struct mh_file *source, struct mh_car *output,
	struct mh_error *error_out)
{
	struct mh_definition_text definition;
	enum mh_error_code status;

	status = mh_car_demo_definition(source, &definition, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_car_config_car_decode(
		definition.data, definition.size, output, error_out);
}
