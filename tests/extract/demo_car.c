#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/car.h"
#include "engine/assets/prepare_demo_car.h"
#include "engine/assets/tbl.h"
#include "engine/catalog/halo_paths.h"
#include "engine/core/binary.h"

#define RECORD_BYTES 5036

static int failures;
#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "line %d: %s\n", __LINE__,             \
				#condition);                                   \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
fixture(unsigned char record[RECORD_BYTES])
{
	size_t offset;

	memset(record, 0, RECORD_BYTES);
	strcpy((char *)record, "Fixture car");
	strcpy((char *)record + 0x104, "Cars/test/body");
	strcpy((char *)record + 0x304, "Cars/test/body.col");
	strcpy((char *)record + 0x404, "Cars/test/lines.lob");

	for (offset = 0x604; offset < 0xe04; offset += 0x100)
		strcpy((char *)record + offset, "Cars/test/wheel");

	strcpy((char *)record + 0xe10, "Cars/test/glow.iff");
	mh_write_u32le(record + 0x100, 302);
	mh_write_u32le(record + 0xe04, 0x123456UL);
	mh_write_u32le(record + 0x1210, 6);

	for (offset = 0x1214; offset < RECORD_BYTES; offset += 4)
		mh_write_u32le(record + offset, 0x3f800000UL); /* 1.0 */

	mh_write_u32le(record + 0x125c, 0x447a0000UL); /* 1000.0 */
	mh_write_u32le(record + 0x12e0, 8);
	mh_write_u32le(record + 0x12e4, 7);
	mh_write_u32le(record + 0x12e8, 6);
}

static int
wrap(const unsigned char *record, struct mh_file *source)
{
	enum mh_error_code status;

	status = mh_tbl_encode_named(
		record, RECORD_BYTES, NULL, &source->data, &source->size, NULL);
	CHECK(status == MH_ERROR_NONE);

	return status == MH_ERROR_NONE;
}

static void
check_invalid(const unsigned char *record)
{
	struct mh_file source, output, saved;

	if (!wrap(record, &source))
		return;

	memset(&saved, 0x5a, sizeof(saved));
	output = saved;
	CHECK(mh_demo_car_prepare(&source, &output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
	free(source.data);
}

static int
check_fixture(void)
{
	unsigned char record[RECORD_BYTES], *text;
	struct mh_file source, output, saved;
	struct mh_car car;
	size_t size, i;
	enum mh_error_code status;

	fixture(record);

	if (!wrap(record, &source))
		return 1;

	status = mh_demo_car_prepare(&source, &output, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		status = mh_tbl_decode(
			output.data, output.size, &text, &size, NULL);
		CHECK(status == MH_ERROR_NONE);

		if (status == MH_ERROR_NONE) {
			status = mh_car_config_car_decode(
				text, size, &car, NULL);
			CHECK(status == MH_ERROR_NONE);

			if (status == MH_ERROR_NONE) {
				CHECK(car.division == 302 && car.gears == 6);
				CHECK(car.has_suspension && car.has_engine &&
					car.has_shadow);
				CHECK(car.mass == 1 && car.max_rpm == 1000);
				CHECK(car.paint_keys[0] == 0x123456UL);
				CHECK(car.speed_level == 8 &&
					car.acceleration_level == 7 &&
					car.grip_level == 6);
				CHECK(car.halos.count == 1 &&
					strcmp(mh_halo_path(&car.halos, 0),
						"Cars/test/glow.iff") == 0);
			}

			free(text);
		}

		free(output.data);
	}

	memset(&saved, 0x5a, sizeof(saved));

	for (i = 0; i < RECORD_BYTES + 4; i++) {
		source.size = i;
		output = saved;
		CHECK(mh_demo_car_prepare(&source, &output, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
	}

	free(source.data);
	memset(record, 'x', 256);
	check_invalid(record);
	fixture(record);
	mh_write_u32le(record + 0x1238, 0x7f800000UL);
	check_invalid(record);
	fixture(record);
	mh_write_u32le(record + 0x1210, 7);
	check_invalid(record);
	fixture(record);
	memset(record + 0xe10, 'x', 1024);
	check_invalid(record);

	return failures != 0;
}

int
main(int argc, char *argv[])
{
	struct mh_file source, output;
	struct mh_error error;
	enum mh_error_code status;
	int success;

	if (argc == 1)
		return check_fixture();

	if (argc != 2) {
		fprintf(stderr, "Usage: test-demo-car [BINARY-CAR]\n");
		return 1;
	}

	status = mh_file_read(argv[1], &source, &error);

	if (status == MH_ERROR_NONE) {
		status = mh_demo_car_prepare(&source, &output, &error);
		free(source.data);
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	success = fwrite(output.data, 1, output.size, stdout) == output.size;
	free(output.data);

	return success && fflush(stdout) == 0 && !ferror(stdout) ? 0 : 1;
}
