"""Small CFG fixtures and saved-setting inspection."""


def encode(values):
	return ''.join(f'{key} {value}\n' for key, value in values.items()).encode()


def read_cfg(path):
	return {pair[0]: pair[1] for line in path.read_text().splitlines()
		if len(pair := line.split(None, 1)) == 2 and not pair[0].startswith('//')}
