"""Read installed catalog metadata for acceptance fixtures."""

from tests.checks.support.tbl import reference_decode


TRACK_ORDER = [8, 1, 3, 4, 7, 6, 2, 5]


CAR_ORDER = [2, 7, 8, 3, 5, 9, 1, 6, 10, 4]


def fields(path):
	result = {}

	for line in reference_decode(path.read_bytes()).decode('ascii').splitlines():
		pair = line.split('//', 1)[0].split(None, 1)

		if len(pair) == 2:
			result[pair[0].lower()] = pair[1].strip()

	return result
