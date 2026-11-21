#include <float.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/settings/decimal.h"

#define SIGNIFICANT_DIGITS 16
#define DECIMAL_PLACES_MAX 9
#define LOCALE_POINT_BYTES 32
#define SCIENTIFIC_BYTES 128

static void
round_digits(
	char digits[SIGNIFICANT_DIGITS + 1], int *exponent, unsigned int places)
{
	int keep, i;

	keep = *exponent + 1 + (int)places;

	if (keep >= 0 && keep < SIGNIFICANT_DIGITS) {
		i = keep;

		if (digits[keep] >= '5') {
			while (i > 0 && digits[i - 1] == '9')
				digits[--i] = '0';

			if (i == 0) {
				memset(digits, '0', SIGNIFICANT_DIGITS);
				digits[0] = '1';
				(*exponent)++;
				keep++;
			} else
				digits[i - 1]++;
		}

		for (i = keep; i < SIGNIFICANT_DIGITS; i++)
			digits[i] = '0';
	} else if (keep < 0)
		memset(digits, '0', SIGNIFICANT_DIGITS);
}

enum mh_error_code
mh_settings_decimal(float value, unsigned int places,
	char output[MH_SETTINGS_DECIMAL_BYTES], struct mh_error *error_out)
{
	char scientific[SCIENTIFIC_BYTES], digits[SIGNIFICANT_DIGITS + 1];
	char text[MH_SETTINGS_DECIMAL_BYTES];
	const char *point;
	size_t point_length, length;
	double magnitude;
	int exponent, i, index, nonzero;

	if (output == NULL || places > DECIMAL_PLACES_MAX ||
		!(value >= -FLT_MAX && value <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid fixed decimal output");

	point = localeconv()->decimal_point;

	for (point_length = 0;
		point_length < LOCALE_POINT_BYTES && point[point_length] != 0;
		point_length++) {
	}

	if (point_length == 0 || point_length == LOCALE_POINT_BYTES)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported numeric locale");

	magnitude = value == 0 ? 0 : value < 0 ? -(double)value : (double)value;
	sprintf(scientific, "%.*e", SIGNIFICANT_DIGITS - 1, magnitude);
	digits[0] = scientific[0];
	memcpy(digits + 1, scientific + 1 + point_length,
		SIGNIFICANT_DIGITS - 1);
	digits[SIGNIFICANT_DIGITS] = 0;
	exponent = (int)strtol(
		scientific + SIGNIFICANT_DIGITS + 1 + point_length, NULL, 10);
	round_digits(digits, &exponent, places);

	length = 0;
	nonzero = 0;

	if (value < 0)
		text[length++] = '-';

	for (i = 0; i <= exponent || i == 0; i++) {
		text[length] = exponent < 0 || i >= SIGNIFICANT_DIGITS
			? '0'
			: digits[i];
		nonzero |= text[length] != '0';
		length++;
	}

	if (places != 0)
		text[length++] = '.';

	for (i = 1; i <= (int)places; i++) {
		index = exponent + i;
		text[length] = index < 0 || index >= SIGNIFICANT_DIGITS
			? '0'
			: digits[index];
		nonzero |= text[length] != '0';
		length++;
	}

	text[length] = 0;
	strcpy(output, !nonzero && text[0] == '-' ? text + 1 : text);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
