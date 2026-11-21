#include <float.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/settings/parse.h"

#define INPUT_BYTES_MAX 1048576UL
#define LINE_BYTES 1024
#define LINE_TOKENS 128
#define EXPONENT_MAX 1024
#define SIGNED_INTEGER_LIMIT 9223372036854775808.0L

int
mh_cfg_boolean(const char *value)
{
	return mh_ascii_equal(value, "true") || mh_ascii_equal(value, "on") ||
		mh_ascii_equal(value, "yes") || mh_ascii_equal(value, "1");
}

/*
 * Decimal/comma parser, with binary64 stores between operations. strtod differs
 * at volume truncation boundaries and accepts other syntax. Bound the original
 * 16-bit exponent loop, which can otherwise never end.
 */
int
mh_cfg_number(const char *text, double *output)
{
	double value, factor, digit, exponent_base;
	unsigned int exponent, byte, i;
	int negative, in_fraction, in_exponent;

	negative = *text == '-';

	if (negative)
		text++;

	value = 0;
	factor = 1;
	exponent_base = 10;
	exponent = 0;
	in_fraction = in_exponent = 0;

	for (;;) {
		byte = (unsigned char)*text++;

		if (byte < '0' && byte != '.' && byte != ',')
			break;

		if (byte == '.' || byte == ',') {
			if (in_fraction) {
				*output = 0;
				return 1;
			}

			in_fraction = 1;
		} else if (byte == 'e') {
			if (*text == '-') {
				text++;
				exponent_base = .1;
			}

			in_exponent = 1;
		} else if (byte >= '0' && byte <= '9') {
			if (in_exponent) {
				if (exponent > EXPONENT_MAX / 10 ||
					exponent * 10 + byte - '0' >
						EXPONENT_MAX)
					return 0;

				exponent = exponent * 10 + byte - '0';
			} else if (in_fraction) {
				factor = (double)((long double)factor *
					(double).1);
				digit = (double)((long double)(byte - '0') *
					factor);
				value = (double)((long double)value + digit);
			} else
				value = (double)((long double)value * 10 +
					(byte - '0'));
		} else {
			*output = 0;
			return 1;
		}
	}

	if (negative)
		value = -value;

	for (i = 0; i < exponent; i++)
		value = (double)((long double)value * exponent_base);

	if (!(value >= -DBL_MAX && value <= DBL_MAX))
		return 0;

	*output = value;

	return 1;
}

int
mh_cfg_integer(long double value, unsigned long *output)
{
	long double magnitude, low;
	unsigned long high, word;

	/*
	 * Original truncates to signed 64 bits, then reads the low 32. C89 has
	 * no 64-bit integer requirement; reduce only after truncation.
	 */
	if (!(value >= -SIGNED_INTEGER_LIMIT && value < SIGNED_INTEGER_LIMIT))
		return 0;

	magnitude = value < 0 ? -value : value;
	high = (unsigned long)(magnitude / MH_U32_MODULUS);
	low = magnitude - (long double)high * MH_U32_MODULUS;
	word = (unsigned long)low;
	*output = value < 0 ? (0UL - word) & MH_U32_MAX : word;

	return 1;
}

void
mh_cfg_choice(const char *value, const char *const *names, unsigned int count,
	unsigned int *target)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		if (mh_ascii_equal(value, names[i]))
			*target = i;
}

static enum mh_error_code
split_line(char *buffer, char *tokens[LINE_TOKENS], unsigned int *token_count,
	struct mh_error *error_out)
{
	char *cursor;
	unsigned int count;

	count = 0;
	cursor = buffer;

	while (*cursor != 0) {
		while (*cursor == ' ' || *cursor == '\t')
			cursor++;

		if (*cursor == 0 || *cursor == '\r' || *cursor == ';' ||
			(*cursor == '/' && cursor[1] == '/'))
			break;

		if (count == LINE_TOKENS)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many settings tokens");

		tokens[count++] = cursor;

		while (*cursor != 0 && *cursor != ' ' && *cursor != '\t' &&
			*cursor != '\r' && *cursor != ';' &&
			!(*cursor == '/' && cursor[1] == '/'))
			cursor++;

		if (*cursor != ' ' && *cursor != '\t') {
			*cursor = 0;
			break;
		}

		*cursor++ = 0;
	}

	*token_count = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_cfg_parse(const unsigned char *text, size_t size,
	enum mh_error_code (*visit)(char **tokens, unsigned int count,
		void *context, struct mh_error *error_out),
	void *context, struct mh_error *error_out)
{
	char buffer[LINE_BYTES], *tokens[LINE_TOKENS];
	size_t offset, length;
	unsigned int count = 0;
	enum mh_error_code status;

	if (text == NULL || visit == NULL || context == NULL ||
		size > INPUT_BYTES_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid settings input");

	offset = 0;

	while (offset < size) {
		length = 0;

		while (offset < size && text[offset] != '\n') {
			/*
			 * Compare unsigned bytes with 32. Preserve legacy
			 * single-byte text; do not interpret UTF-8 or use
			 * locale whitespace rules for bytes such as 0xa0.
			 */
			if (text[offset] < 32 && text[offset] != '\r' &&
				text[offset] != '\t')
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"settings contain unsupported control bytes");

			if (length == sizeof(buffer) - 1)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"settings line too long");

			buffer[length++] = (char)text[offset++];
		}

		if (offset < size)
			offset++;

		buffer[length] = 0;
		status = split_line(buffer, tokens, &count, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = visit(tokens, count, context, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
