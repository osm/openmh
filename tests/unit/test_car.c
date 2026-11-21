#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/tbl_fixture.h"
#include "engine/assets/car.h"
#include "engine/assets/tbl.h"
#include "engine/catalog/car.h"
#include "engine/catalog/halo_paths.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define MUTATION_MULTIPLIER 1664525UL
#define MUTATION_INCREMENT 1013904223UL

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static const char car_text[] =
	"// Synthetic car, deliberately unlike the shipped values.\r\n"
	"CarName Example Car  // inline comment\r\n"
	"Division 410\nHalo head.iff\nHalo tail.iff\n"
	"filename cars\\test\\body_obj\n"
	"Wheel1 cars\\test\\fl_obj cars\\test\n"
	"Wheel2 cars\\test\\fr_obj cars\\test\n"
	"Wheel3 cars\\test\\br_obj cars\\test\n"
	"Wheel4 cars\\test\\bl_obj cars\\test\n"
	"PhyWheelFL -2 3 4\nPhyWheelFR 2 3 4\n"
	"PhyWheelBR 2 3 -5\nPhyWheelBL -2 3 -5\n"
	"PhySpringLength .5\nUnknownField 123\n";

static void
check_tbl(void)
{
	unsigned char bad[516];
	unsigned char *text;
	size_t size;
	size_t i;
	enum mh_error_code status;

	status = mh_tbl_decode(
		tbl_fixture, sizeof(tbl_fixture), &text, &size, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(size == 512 && text[512] == 0);

		for (i = 0; i < size; i++)
			CHECK(text[i] == ((i * 37 + 11) & 255));

		free(text);
	}

	for (i = 0; i < 4; i++) {
		text = bad;
		size = 123;
		CHECK(mh_tbl_decode(tbl_fixture, i, &text, &size, NULL) !=
			MH_ERROR_NONE);
		CHECK(text == bad && size == 123);
	}

	memcpy(bad, tbl_fixture, sizeof(bad));
	bad[3] = 7;
	CHECK(mh_tbl_decode(bad, sizeof(bad), &text, &size, NULL) !=
		MH_ERROR_NONE);
	bad[3] = 5;
	bad[0] = 0;
	CHECK(mh_tbl_decode(bad, sizeof(bad), &text, &size, NULL) !=
		MH_ERROR_NONE);
	status = mh_tbl_decode(tbl_fixture, 4, &text, &size, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(size == 0 && text[0] == 0);
		free(text);
	}
}

static void
check_named_tbl(void)
{
	unsigned char payload[512], *encoded, *decoded;
	char name[130];
	size_t i, name_length, encoded_size, decoded_size, length;
	enum mh_error_code status;

	for (i = 0; i < sizeof(payload); i++)
		payload[i] = (unsigned char)((i * 37 + 11) & 255);

	status = mh_tbl_encode_named(
		payload, sizeof(payload), NULL, &encoded, &encoded_size, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(encoded_size == sizeof(tbl_fixture));
		CHECK(memcmp(encoded, tbl_fixture, sizeof(tbl_fixture)) == 0);
		free(encoded);
	}

	for (name_length = 0; name_length < sizeof(name); name_length++) {
		memset(name, 'x', name_length);
		name[name_length] = 0;

		for (length = 0; length <= sizeof(payload); length += 128) {
			status = mh_tbl_encode_named(payload, length, name,
				&encoded, &encoded_size, NULL);
			CHECK(status == MH_ERROR_NONE);

			if (status != MH_ERROR_NONE)
				continue;

			status = mh_tbl_decode_named(encoded, encoded_size,
				name, &decoded, &decoded_size, NULL);
			CHECK(status == MH_ERROR_NONE);

			if (status == MH_ERROR_NONE) {
				CHECK(decoded_size == length &&
					memcmp(decoded, payload, length) == 0);
				CHECK(decoded[length] == 0);
				free(decoded);
			}

			free(encoded);
		}
	}

	encoded = payload;
	encoded_size = 17;
	CHECK(mh_tbl_encode_named(payload, (size_t)-1, NULL, &encoded,
		&encoded_size, NULL) != MH_ERROR_NONE);
	CHECK(mh_tbl_encode_named(NULL, 0, NULL, &encoded, &encoded_size,
		NULL) != MH_ERROR_NONE);
	CHECK(encoded == payload && encoded_size == 17);
}

static void
check_car(void)
{
	struct mh_car car;
	struct mh_car saved;
	double offsets[4][3];
	unsigned char data[2048];
	unsigned long seed;
	enum mh_error_code status;
	size_t size;
	size_t i;
	char *number;
	static const char physics[] =
		"PhyWeight 1450\nPhyCenterMass .1 .6 -.2\nPhySpringStrength 300000\n";
	static const char *invalid_fields[] = {"Halo", "Halo a b",
		"TopSpeed 300", "TopSpeed nan", "TopSpeed 300 extra",
		"TopSpeed 1e9",
		"TopSpeed 300\nAcceleration 2.5\nHandling 7\nTopSpeed 310",
		"LineObjectName", "LineObjectName a b",
		"LineObjectName a\nLineObjectName b", "Colour1 1 2 3",
		"Colour1 1 2", "Colour1 1.5 2 3", "Colour1 2147483648 2 3",
		"Colour1 nan 2 3", "Colour1 1.0 2 3", "Colour1 1e1 2 3",
		"Colour1 1 2 3\nColour1 4 5 6", "CollisionName",
		"CollisionName a b", "CollisionName a\nCollisionName b",
		"PhyWeight 0", "PhyWeight -1", "PhyWeight nan",
		"PhyWeight 10001", "PhyWeight 1200", "PhyCenterMass 0 0",
		"PhyCenterMass 0 0 0 extra", "PhySpringStrength -1",
		"PhySpringStrength 1e10", "PhySpringStrength inf",
		"PhyTurnForce -1", "PhyTurnForce nan", "PhyTurnForce inf",
		"PhyTurnForce 1e10", "PhyTurnForce 100 extra", "PhyTurnForce",
		"GripLevel -1", "GripLevel 1.5", "GripLevel 1001",
		"GripLevel nan", "GripLevel 8", "PhyBrakeForce .5",
		"GripLevel 8\nPhyBrakeForce 0",
		"GripLevel 8\nPhyBrakeForce 1e-300",
		"GripLevel 8\nPhyBrakeForce inf", "PhyCornerFL 1 2 3",
		"PhyCornerFL 1 nan 3", "PhyCornerFL 1 2",
		"PhyCornerFL 1 2 3\nPhyCornerFR 1 2 3\nPhyCornerBR 1 2 3",
		"PhyCornerFL 1 2 3\nPhyCornerFR 1 2 3\nPhyCornerBR 1 2 3\nPhyCornerBL 1 2 3\nPhyCornerFL 1 2 3"};

	size = strlen(car_text);
	status = mh_car_config_car_decode(
		(const unsigned char *)car_text, size, &car, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(strcmp(car.name, "Example Car") == 0);
		CHECK(car.has_division && car.division == 410);
		CHECK(car.halos.count == 2);
		CHECK(strcmp(mh_halo_path(&car.halos, 0), "head.iff") == 0);
		CHECK(strcmp(mh_halo_path(&car.halos, 1), "tail.iff") == 0);
		CHECK(strcmp(car.body, "cars\\test\\body_obj") == 0);
		CHECK(strcmp(car.wheels[3], "cars\\test\\bl_obj") == 0);
		mh_car_reset_offsets(&car, offsets);
		CHECK(offsets[0][0] == -2.5 && offsets[0][1] == 2.5 &&
			offsets[0][2] == 4);
		CHECK(offsets[2][0] == 2.5 && offsets[2][2] == -5);
	}

	saved = car;
	memcpy(data, car_text, size + 1);
	strcat((char *)data, "Division 100\n");
	CHECK(mh_car_config_car_decode(data, strlen((char *)data), &car,
		NULL) != MH_ERROR_NONE &&
		memcmp(&car, &saved, sizeof(car)) == 0);
	CHECK(!car.has_suspension);
	CHECK(!car.has_steering);
	memcpy(data, car_text, size + 1);
	strcat((char *)data, physics);
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) == MH_ERROR_NONE);
	CHECK(car.has_suspension && car.mass == 1450 &&
		car.spring_strength == 300000);
	CHECK(car.center_mass[0] == .1 && car.center_mass[1] == .6 &&
		car.center_mass[2] == -.2);
	strcat((char *)data, "PhyTurnForce 12345\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) == MH_ERROR_NONE);
	CHECK(car.has_steering && car.turn_force == 12345);
	CHECK(!car.has_grip);
	strcat((char *)data, "GripLevel 12\nPhyBrakeForce .75\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) == MH_ERROR_NONE);
	CHECK(car.has_grip && car.grip_level == 12 && car.brake_force == .75);
	CHECK(!car.has_corners);
	CHECK(!car.has_paint_keys);
	strcat((char *)data,
		"Colour1 -1 256 257\ncolour2 1 2 3\nColour3 255 254 253\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) == MH_ERROR_NONE);
	CHECK(car.has_paint_keys && car.paint_keys[0] == 0xff0001UL &&
		car.paint_keys[1] == 0x010203UL &&
		car.paint_keys[2] == 0xfffefdUL);
	strcat((char *)data,
		"PhyCornerFL -1 99 2\nPhyCornerFR 1 98 2\nPhyCornerBR 1 97 -2\nPhyCornerBL -1 96 -2\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) == MH_ERROR_NONE);
	CHECK(car.has_corners && car.corners[0][1] == 99 &&
		car.corners[3][2] == -2);
	strcat((char *)data, "CollisionName cars\\test\\hull.col\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(car.collision_file, "cars\\test\\hull.col") == 0);
	strcat((char *)data,
		"LineObjectName cars\\test\\preview.lob\nTopSpeed 299.5\nAcceleration 2.75\nHandling 7.25\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) == MH_ERROR_NONE);
	CHECK(car.has_statistics && car.top_speed == 299.5f &&
		car.acceleration == 2.75f && car.handling == 7.25f);
	CHECK(strcmp(car.line_object, "cars\\test\\preview.lob") == 0);
	saved = car;
	strcat((char *)data, "PhyTurnForce 12345\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) != MH_ERROR_NONE);
	memcpy(data, car_text, size + 1);
	strcat((char *)data, physics);
	strcat((char *)data, "PhySpringStrength 300000\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) != MH_ERROR_NONE);

	for (i = 0; i < sizeof(invalid_fields) / sizeof(invalid_fields[0]);
		i++) {
		memcpy(data, car_text, size + 1);
		strcat((char *)data, invalid_fields[i]);
		CHECK(mh_car_config_car_decode(data, strlen((char *)data), &car,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&car, &saved, sizeof(car)) == 0);
	}

	CHECK(mh_car_config_car_decode((const unsigned char *)car_text, 30,
		&car, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&car, &saved, sizeof(car)) == 0);
	memcpy(data, car_text, size + 1);
	strcat((char *)data, "Wheel1 x y\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) != MH_ERROR_NONE);
	memcpy(data, car_text, size + 1);
	data[70] = 0;
	CHECK(mh_car_config_car_decode(data, size, &car, NULL) !=
		MH_ERROR_NONE);
	memcpy(data, car_text, size + 1);
	number = strstr((char *)data, ".5");
	strcpy(number, "1e9999\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) != MH_ERROR_NONE);
	strcpy(number, "nan\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) != MH_ERROR_NONE);
	strcpy(number, "-1\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) != MH_ERROR_NONE);
	memset(data, 'x', sizeof(data));
	CHECK(mh_car_config_car_decode(data, sizeof(data), &car, NULL) !=
		MH_ERROR_NONE);
	seed = 193;

	for (i = 0; i < 2000; i++) {
		memcpy(data, car_text, size);
		seed = (seed * MUTATION_MULTIPLIER + MUTATION_INCREMENT) &
			MH_U32_MAX;
		data[seed % size] = (unsigned char)(seed >> 24);
		memset(&car, 0xa5, sizeof(car));
		memcpy(&saved, &car, sizeof(saved));
		status = mh_car_config_car_decode(data, size, &car, NULL);

		if (status != MH_ERROR_NONE)
			CHECK(memcmp(&car, &saved, sizeof(car)) == 0);
	}
}

static void
check_engine_fields(void)
{
	static const char tuning[] =
		"PhyMinRPM 1200\nPhyMaxRPM 9000\n"
		"PhyAccForce 2700\nAccLevel 8\nSpeedLevel 4\n";
	static const char *bad[] = {"Gears 2\n",
		"Gears 0\nPhyGearRatio 50 60\n",
		"Gears 7\nPhyGearRatio 50 60 70 80 90 100 110 120\n",
		"Gears 2.5\nPhyGearRatio 50 60 70\n",
		"Gears 2\nPhyGearRatio 50 60\n",
		"Gears 2\nPhyGearRatio 50 60 70 80\n",
		"Gears 2\nPhyGearRatio 50 0 70\n",
		"Gears 2\nPhyGearRatio 50 nan 70\n",
		"Gears 2\nPhyGearRatio 50 60 1e999\n",
		"Gears 2\nPhyGearRatio 50 60 70 extra\n",
		"Gears 2\nPhyGearRatio 50 60 70\nPhyMinRPM 1000\n",
		"Gears 2\nPhyGearRatio 50 60 70\nGears 2\n"};

	unsigned char data[2048];
	struct mh_car car;
	struct mh_car saved;
	unsigned int i;
	char *value;

	strcpy((char *)data, car_text);
	strcat((char *)data, tuning);

	/* Gear ratios may precede Gears; there is one reverse entry. */
	strcat((char *)data,
		"PhyGearRatio 50 60 80\nGears 2\nTransmission Manual\n");
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) == MH_ERROR_NONE);
	CHECK(car.has_engine && car.gears == 2 && car.gear_ratio[0] == 50 &&
		car.gear_ratio[2] == 80);
	CHECK(car.min_rpm == 1200 && car.max_rpm == 9000 &&
		car.acceleration_force == 2700);
	CHECK(car.acceleration_level == 8 && car.speed_level == 4);
	saved = car;
	value = strstr((char *)data, "9000");
	memcpy(value, "1000", 4);
	CHECK(mh_car_config_car_decode(
		data, strlen((char *)data), &car, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&car, &saved, sizeof(car)) == 0);

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		strcpy((char *)data, car_text);
		strcat((char *)data, tuning);
		strcat((char *)data, bad[i]);
		CHECK(mh_car_config_car_decode(data, strlen((char *)data), &car,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&car, &saved, sizeof(car)) == 0);
	}
}

static void
check_shadow_fields(void)
{
	struct mh_car car, saved;
	char text[2048], row[160];
	unsigned int i;
	static const char *bad[] = {"shadowpoints1 0 0 0",
		"shadowpoints1 0 0 0 1 0 0 2 0 0 3 0 0",
		"shadowpoints1 0 0 0 1 0 0 2 0 0 3 0 nan",
		"shadowpoints1 0 0 0 1 0 0 2 0 0 3 0 10001",
		"shadowpoints1 0 0 0 1 0 0 2 0 0 3 0 0 extra"};

	strcpy(text, car_text);
	CHECK(mh_car_config_car_decode((const unsigned char *)text,
		strlen(text), &car, NULL) == MH_ERROR_NONE &&
		!car.has_shadow);

	for (i = 1; i <= 4; i++) {
		sprintf(row,
			"ShadowPoints%u -1 .1 %u -.5 .2 %u .5 .3 %u 1 .4 %u\n",
			i, i, i, i, i);
		strcat(text, row);
	}

	CHECK(mh_car_config_car_decode((const unsigned char *)text,
		strlen(text), &car, NULL) == MH_ERROR_NONE &&
		car.has_shadow);
	CHECK(car.shadow_points[0][1] == .1f && car.shadow_points[15][2] == 4);
	saved = car;
	strcat(text, row);
	CHECK(mh_car_config_car_decode((const unsigned char *)text,
		strlen(text), &car, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&car, &saved, sizeof(car)) == 0);

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		strcpy(text, car_text);
		strcat(text, bad[i]);
		CHECK(mh_car_config_car_decode((const unsigned char *)text,
			strlen(text), &car, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&car, &saved, sizeof(car)) == 0);
	}
}

static int
stream_tbl_encode(const char *name)
{
	unsigned char input[65536], *output;
	size_t size, output_size;
	enum mh_error_code status;
	int failed;

	size = fread(input, 1, sizeof(input), stdin);

	if (ferror(stdin) || (size == sizeof(input) && getchar() != EOF))
		return 1;

	status = mh_tbl_encode_named(
		input, size, name, &output, &output_size, NULL);

	if (status != MH_ERROR_NONE)
		return 1;

	failed = fwrite(output, 1, output_size, stdout) != output_size;
	free(output);

	return failed;
}

int
main(int argc, char *argv[])
{
	if (argc == 3 && strcmp(argv[1], "--tbl-encode") == 0)
		return stream_tbl_encode(argv[2]);

	check_tbl();
	check_named_tbl();
	check_car();
	check_engine_fields();
	check_shadow_fields();

	if (failures != 0)
		return 1;

	puts("TBL golden bytes, car configuration, reset pose and malformed inputs passed");

	return 0;
}
