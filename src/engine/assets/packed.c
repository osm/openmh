#include <stdlib.h>
#include <string.h>

#include "engine/assets/packed.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define RESOURCE_LIMIT (16UL * 1024 * 1024)
#define DECODED_SIZE_MASK 0x7ffffffUL
#define TABLE_SIZE_MASK 0x3fffffUL

enum {
	HEADER_BYTES = 4,
	BYTE_BITS = 8,
	RLE_MIN_RUN = 4,
	METHOD_SHIFT = 27,
	METHOD_MASK = 15,
	ENTRY_WIDTH_SHIFT = 30,
	SYMBOL_BIAS_SHIFT = 22
};

/* Values are encoded in the packed header; repeat widths depend on them. */
enum packed_method {
	PACK_COPY = 0,
	PACK_RLE = 1,
	PACK_HUFFMAN = 2,
	PACK_HUFFMAN_UNARY = 3,
	PACK_HUFFMAN_ADJACENT_2 = 4,
	PACK_HUFFMAN_ADJACENT_3 = 5,
	PACK_HUFFMAN_REPEAT_2 = 6,
	PACK_HUFFMAN_REPEAT_3 = 7,
	PACK_HUFFMAN_REPEAT_4 = 8
};

struct bitstream {
	const unsigned char	*data;
	size_t			size, offset;
	unsigned int		bits_left, byte;
};

struct huffman_tree {
	const unsigned char	*data;
	unsigned long		entry_count;
	unsigned int		entry_bytes, symbol_bias;
};

static int
read_bit(struct bitstream *stream, unsigned int *value)
{
	if (stream->bits_left == 0) {
		if (stream->offset == stream->size)
			return 0;

		stream->byte = stream->data[stream->offset++];
		stream->bits_left = BYTE_BITS;
	}

	stream->bits_left--;
	*value = (stream->byte >> stream->bits_left) & 1;

	return 1;
}

static int
decode_rle(const unsigned char *data, size_t size, unsigned char *output,
	size_t decoded_size)
{
	size_t input_offset, output_offset, count;
	unsigned int marker, value;

	if (size == 0)
		return 0;

	marker = data[0];
	input_offset = 1;
	output_offset = 0;

	while (output_offset < decoded_size) {
		if (input_offset == size)
			return 0;

		value = data[input_offset++];
		count = 1;

		if (value == marker) {
			if (input_offset == size)
				return 0;

			value = data[input_offset++];

			if (value != marker) {
				if (input_offset == size)
					return 0;

				count = (size_t)data[input_offset++] +
					RLE_MIN_RUN;
			}
		}

		if (count > decoded_size - output_offset)
			return 0;

		memset(output + output_offset, (int)value, count);
		output_offset += count;
	}

	return 1;
}

static int
read_symbol(const struct huffman_tree *tree, struct bitstream *stream,
	unsigned long *symbol)
{
	unsigned long node, value;
	unsigned int branch;
	size_t offset;

	node = 0;

	for (;;) {
		if (!read_bit(stream, &branch) || node >= tree->entry_count - 1)
			return 0;

		offset = HEADER_BYTES +
			(size_t)(node + branch) * tree->entry_bytes;
		value = tree->entry_bytes == 1
			? tree->data[offset]
			: (tree->entry_bytes == 2
					? mh_read_u16le(tree->data + offset)
					: mh_read_u32le(tree->data + offset));

		if (value & 1) {
			value = (value >> 1) + tree->symbol_bias;

			if (value > 255)
				return 0;

			*symbol = value;

			return 1;
		}

		if (value >= tree->entry_count - node)
			return 0;

		node += value;
	}
}

static int
decode_huffman(const unsigned char *data, size_t size, unsigned char *output,
	size_t decoded_size, unsigned int method)
{
	struct bitstream stream;
	struct huffman_tree tree;
	unsigned long header, symbol, table_size;
	unsigned int branch, i, run, repeat_bits;
	size_t output_offset;

	if (size < HEADER_BYTES)
		return 0;

	header = mh_read_u32le(data);
	table_size = header & TABLE_SIZE_MASK;
	tree.entry_bytes = (unsigned int)(header >> ENTRY_WIDTH_SHIFT) + 1;
	tree.symbol_bias = (unsigned int)((header >> SYMBOL_BIAS_SHIFT) & 255);

	if (tree.entry_bytes == 3 || table_size < HEADER_BYTES ||
		table_size > size ||
		(table_size - HEADER_BYTES) % tree.entry_bytes != 0)
		return 0;

	tree.entry_count = (table_size - HEADER_BYTES) / tree.entry_bytes;

	if (tree.entry_count < 2 || tree.entry_count % 2 != 0)
		return 0;

	tree.data = data;
	stream.data = data;
	stream.size = size;
	stream.offset = (size_t)table_size;
	stream.bits_left = 0;
	stream.byte = 0;
	output_offset = 0;

	while (output_offset < decoded_size) {
		if (!read_symbol(&tree, &stream, &symbol))
			return 0;

		output[output_offset++] = (unsigned char)symbol;

		if (method == PACK_HUFFMAN)
			continue;

		/* Zero bits repeat the symbol, one terminates the run. */
		if (method == PACK_HUFFMAN_UNARY) {
			for (;;) {
				if (!read_bit(&stream, &branch))
					return 0;

				if (branch != 0)
					break;

				if (output_offset == decoded_size)
					return 0;

				output[output_offset++] = (unsigned char)symbol;
			}

			continue;
		}

		/*
		 * Code zero emits no second byte. Otherwise emit the same or an
		 * adjacent symbol, wrapping at byte boundaries.
		 */
		if (method == PACK_HUFFMAN_ADJACENT_2 ||
			method == PACK_HUFFMAN_ADJACENT_3) {
			run = 0;

			for (i = 0; i < method - 2; i++) {
				if (!read_bit(&stream, &branch))
					return 0;

				run = run * 2 + branch;
			}

			if (run != 0) {
				if (output_offset == decoded_size)
					return 0;

				symbol = run & 1 ? symbol + run / 2
						: symbol + 256 - run / 2;
				output[output_offset++] =
					(unsigned char)(symbol & 255);
			}

			continue;
		}

		/*
		 * Methods 6..8 use a flag followed by 2..4 repeat-count bits.
		 */
		if (!read_bit(&stream, &branch))
			return 0;

		run = 0;
		repeat_bits = method - 4;

		if (branch) {
			for (i = 0; i < repeat_bits; i++) {
				if (!read_bit(&stream, &branch))
					return 0;

				run = run * 2 + branch;
			}

			run++;
		}

		if (run > decoded_size - output_offset)
			return 0;

		memset(output + output_offset, (int)symbol, run);
		output_offset += run;
	}

	return 1;
}

enum mh_error_code
mh_packed_decode(const unsigned char *data, size_t size, unsigned char **output,
	size_t *output_size, struct mh_error *error_out)
{
	unsigned long header;
	unsigned int method;
	unsigned char *result;
	size_t decoded_size, i;
	int success;

	if (data == NULL || output == NULL || output_size == NULL ||
		size < HEADER_BYTES || size > RESOURCE_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid packed resource");

	header = mh_read_u32le(data);
	decoded_size = (size_t)(header & DECODED_SIZE_MASK);
	method = (unsigned int)((header >> METHOD_SHIFT) & METHOD_MASK);

	if (decoded_size == 0 || decoded_size > RESOURCE_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid packed resource length");

	if (method > PACK_HUFFMAN_REPEAT_4)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported packed resource method");

	result = (unsigned char *)malloc(decoded_size + 1);

	if (result == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory unpacking resource");

	data += HEADER_BYTES;
	size -= HEADER_BYTES;

	if (method == PACK_COPY) {
		success = decoded_size <= size;

		if (success)
			memcpy(result, data, decoded_size);
	} else if (method == PACK_RLE)
		success = decode_rle(data, size, result, decoded_size);
	else
		success = decode_huffman(
			data, size, result, decoded_size, method);

	if (!success) {
		free(result);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"malformed packed resource");
	}

	if (header & MH_U32_SIGN_BIT)
		for (i = 1; i < decoded_size; i++)
			result[i] =
				(unsigned char)((result[i] + result[i - 1]) &
					255);

	result[decoded_size] = 0;
	*output = result;
	*output_size = decoded_size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
