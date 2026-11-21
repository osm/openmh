#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/spr.h"
#include "engine/core/error.h"

enum {
	FRAME_INDEX_MAX = 255,
	INDEXED_PIXEL_BYTES = 2,
	LITERAL_PIXEL_BYTES = 3,
	RGBA_PIXEL_BYTES = 4
};

enum output_format {
	OUTPUT_RGBA,
	OUTPUT_INDICES,
	OUTPUT_WORDS
};

struct sprite_output {
	FILE			*stream;
	unsigned int		green_bits;
	enum output_format	format;
};

static enum mh_error_code
write_frame(const struct mh_spr *archive, unsigned int index,
	unsigned int frame, int columns, const struct sprite_output *output,
	struct mh_error *error_out)
{
	struct mh_image image;
	struct mh_sprite_indexed indexed;
	struct mh_sprite_words literal;
	unsigned char *pixels;
	size_t bytes;
	enum mh_error_code status;

	if (output->format == OUTPUT_INDICES) {
		status = mh_sprite_spr_decode_indexed(
			archive, index, frame, &indexed, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		pixels = indexed.pixels;
		bytes = (size_t)indexed.width * indexed.height *
			INDEXED_PIXEL_BYTES;
	} else if (output->format == OUTPUT_WORDS) {
		status = mh_sprite_spr_decode_literal(
			archive, index, frame, columns, &literal, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		pixels = literal.pixels;
		bytes = (size_t)literal.width * literal.height *
			LITERAL_PIXEL_BYTES;
	} else {
		status = output->green_bits == 0
			? mh_sprite_spr_decode_rgba(
				archive, index, frame, &image, error_out)
			: mh_sprite_spr_decode_literal_rgba(archive, index,
				frame, columns, output->green_bits, &image,
				error_out);

		if (status != MH_ERROR_NONE)
			return status;

		pixels = image.rgba;
		bytes = (size_t)image.width * image.height * RGBA_PIXEL_BYTES;
	}

	if (fwrite(pixels, 1, bytes, output->stream) != bytes)
		status = mh_error_set(
			error_out, MH_ERROR_IO, "sprite output failed");

	free(pixels);

	return status;
}

int
main(int argc, char *argv[])
{
	struct mh_file file;
	struct mh_spr archive;
	const struct mh_spr_entry *entry;
	struct mh_error error;
	enum mh_error_code status;
	const char *path;
	char *end;
	unsigned long number;
	unsigned int i, frame, selected, first, last, width, height;
	struct sprite_output output;
	int all, indices, words, argument, remaining, columns;

	argument = 2;
	output.green_bits = 0;

	if (argc > 2 && strcmp(argv[2], "--rgb555") == 0) {
		output.green_bits = 5;
		argument++;
	}

	if (argc > 2 && strcmp(argv[2], "--rgb565") == 0) {
		output.green_bits = 6;
		argument++;
	}

	remaining = argc - argument;

	if (remaining != 0 && remaining != 2 && remaining != 3)
		goto usage;

	indices = remaining == 2 && strcmp(argv[argument], "--indices") == 0;
	words = remaining == 2 && strcmp(argv[argument], "--words") == 0;
	all = remaining == 2 &&
		(strcmp(argv[argument], "--all") == 0 || indices || words);

	if ((remaining == 2 && !all) || (indices && output.green_bits != 0) ||
		(words && output.green_bits == 0))
		goto usage;

	number = 0;

	if (remaining == 3) {
		number = strtoul(argv[argument + 1], &end, 10);

		if (*argv[argument + 1] == 0 || *end != 0 ||
			number > FRAME_INDEX_MAX)
			goto usage;
	}

	if (indices)
		output.format = OUTPUT_INDICES;
	else if (words)
		output.format = OUTPUT_WORDS;
	else
		output.format = OUTPUT_RGBA;

	memset(&file, 0, sizeof(file));
	memset(&archive, 0, sizeof(archive));
	output.stream = NULL;
	status = mh_file_read(argv[1], &file, &error);

	if (status == MH_ERROR_NONE)
		status = mh_sprite_spr_read(
			file.data, file.size, &archive, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	selected = archive.count;

	if (remaining == 3) {
		entry = mh_sprite_spr_find(&archive, argv[argument]);

		if (entry == NULL) {
			status = mh_error_set(&error, MH_ERROR_NOT_FOUND,
				"sprite name not found");
			goto done;
		}

		selected = (unsigned int)(entry - archive.entries);
	}

	if (remaining > 0) {
		path = argv[argument + (all ? 1 : 2)];
		output.stream = fopen(path, "wb");

		if (output.stream == NULL) {
			status = mh_error_set(&error, MH_ERROR_IO,
				"cannot create sprite output");
			goto done;
		}
	}

	for (i = 0; i < archive.count; i++) {
		if (selected != archive.count && i != selected)
			continue;

		entry = &archive.entries[i];

		/*
		 * Original software HUD binds meter to the column
		 * dispatcher.
		 */
		columns = output.green_bits != 0 &&
			strcmp(entry->name, "meter") == 0;

		if (output.green_bits == 0)
			width = entry->width;
		else if (columns)
			width = entry->height;
		else
			width = entry->width / 2;

		height = columns ? entry->width : entry->height;
		printf("%s %u %u %u\n", entry->name, width, height,
			entry->frames);
		first = all ? 0 : (unsigned int)number;
		last = all ? entry->frames : first + 1;

		if (output.stream != NULL)
			for (frame = first; frame < last; frame++) {
				status = write_frame(&archive, i, frame,
					columns, &output, &error);

				if (status != MH_ERROR_NONE)
					goto done;
			}
	}

	if (ferror(stdout))
		status = mh_error_set(
			&error, MH_ERROR_IO, "sprite listing failed");

done:
	if (output.stream != NULL && fclose(output.stream) != 0 &&
		status == MH_ERROR_NONE)
		status = mh_error_set(
			&error, MH_ERROR_IO, "sprite output close failed");

	mh_sprite_spr_free(&archive);
	free(file.data);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "SPR: %s\n", error.message);

	return status != MH_ERROR_NONE;

usage:
	fprintf(stderr,
		"Usage: mh-spr-info FILE [--rgb555|--rgb565] [NAME FRAME OUTPUT.rgba | --all OUTPUT.rgba | --indices OUTPUT.idx | --words OUTPUT.bin]\n");

	return 2;
}
