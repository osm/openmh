#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/demo.h"
#include "engine/core/error.h"
#include "engine/network/protocol.h"
#include "tools/network/vehicle_output.h"

static void
print_quoted(const char *text)
{
	unsigned int byte;

	putchar('"');

	while (*text != '\0') {
		byte = (unsigned char)*text++;

		if (byte < 32 || byte > 126 || byte == '"' || byte == '\\')
			printf("\\x%02x", byte);
		else
			putchar((int)byte);
	}

	putchar('"');
}

static void
print_metadata(const struct mh_demo *demo)
{
	const struct mh_demo_player *player;
	unsigned int i, j, k, slots;

	printf("demo %lu %lu %u %u %lu %lu\n", demo->flags, demo->laps,
		demo->player_count, demo->local_player,
		(unsigned long)demo->frame_bytes,
		(unsigned long)demo->frame_count);
	printf("track ");
	print_quoted(demo->track);
	putchar('\n');

	slots = mh_demo_slots(demo->version);

	if (demo->simulation_rate != 0)
		printf("simulation timing=%u rate=%u\n", demo->timing_version,
			demo->simulation_rate);

	for (i = 0; i < slots; i++) {
		player = &demo->players[i];
		printf("player %u %lu ", i, player->active);
		print_quoted(player->name);
		putchar(' ');
		print_quoted(player->team);
		putchar(' ');
		print_quoted(player->car);

		for (j = 0; j < 4; j++)
			printf(" %lu", player->colors[j]);

		printf(" %lu %lu\n", player->gear_mode, player->control_type);

		if (demo->version != MH_DEMO_VERSION_25) {
			printf("horn %u ", i);
			print_quoted(player->horn);
			putchar('\n');
		}

		for (j = 0; j < MH_DEMO_CROSSING_ROWS; j++) {
			printf("crossing %u %u", i, j);

			for (k = 0; k < 4; k++)
				printf(" %lu", player->crossings[j][k]);

			putchar('\n');
		}
	}
}

int
main(int argc, char *argv[])
{
	struct mh_file file;
	struct mh_demo demo;
	struct mh_demo_frame frame;
	struct mh_net_vehicle vehicle;
	struct mh_error error;
	enum mh_error_code status;
	size_t frame_index;
	unsigned int i;
	int show_frames, show_header;
	unsigned char header[MH_DEMO_MAX_HEADER_BYTES];
	size_t header_size;

	if (argc != 2 &&
		(argc != 3 ||
			(strcmp(argv[2], "--frames") != 0 &&
				strcmp(argv[2], "--header") != 0))) {
		fprintf(stderr,
			"Usage: mh-demo-info RECORDING.MDE [--frames | --header]\n");
		return 1;
	}

	show_frames = argc == 3 && strcmp(argv[2], "--frames") == 0;
	show_header = argc == 3 && strcmp(argv[2], "--header") == 0;
	status = mh_file_read(argv[1], &file, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	status = mh_demo_read(file.data, file.size, &demo, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		free(file.data);
		return 1;
	}

	if (show_header) {
		header_size = mh_demo_header_size(demo.version);
		status = mh_demo_header_write(
			&demo, header, sizeof(header), &error);
		free(file.data);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "%s\n", error.message);
			return 1;
		}

		return fwrite(header, 1, header_size, stdout) != header_size ||
			fflush(stdout) != 0;
	}

	print_metadata(&demo);

	for (frame_index = 0; frame_index < demo.frame_count; frame_index++) {
		status = mh_demo_frame_read(&demo, frame_index, &frame, &error);

		if (status != MH_ERROR_NONE)
			break;

		if (show_frames)
			printf("frame %lu %lu %lu %u %u\n",
				(unsigned long)frame_index, frame.milliseconds,
				frame.flags, frame.active_mask,
				frame.unavailable_mask);

		for (i = 0; i < MH_DEMO_MAX_PLAYERS; i++)
			if (frame.vehicles[i] != NULL) {
				status =
					mh_net_vehicle_decode(frame.vehicles[i],
						MH_DEMO_VEHICLE_BYTES, &vehicle,
						&error);

				if (status != MH_ERROR_NONE)
					break;

				if (show_frames) {
					printf("vehicle %lu %u",
						(unsigned long)frame_index, i);
					mh_tool_print_vehicle(&vehicle);
					putchar('\n');
				}
			}

		if (status != MH_ERROR_NONE)
			break;
	}

	free(file.data);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "frame %lu: %s\n", (unsigned long)frame_index,
			error.message);
		return 1;
	}

	printf("validated %lu frames\n", (unsigned long)demo.frame_count);

	return fflush(stdout) != 0;
}
