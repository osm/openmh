"""Check optional server configuration with entirely synthetic course assets."""

import argparse
from pathlib import Path
import subprocess
import tempfile

from tests.checks.support.server import synthetic_config, wait_banner
from tests.checks.support.tbl import reference_encode


def track(path, name, route='fixture.mot'):
	definition = (f'TrackName {name}\nSplineName {route}\n'
		'EnvMapName one\nEnvMapName two\nEnvMapName three\n')
	path.write_bytes(reference_encode(definition.encode()))


def check(binary, root):
	config = synthetic_config(root)
	config.unlink()
	game = root / 'Game'
	game.mkdir()
	track(game / 'Track1.trk', 'Harbor')
	track(game / 'Track2.trk', 'Ridge')

	def run(*options, success=True, cwd=root):
		result = subprocess.run([str(binary), '--check', *map(str, options)],
			cwd=cwd, capture_output=True, text=True, timeout=15)
		output = result.stdout + result.stderr
		assert (result.returncode == 0) == success, output

		return output

	empty = root / 'empty'
	empty.mkdir()

	for options, directory in (((), empty), (('--assets', empty), root)):
		output = run(*options, success=False, cwd=directory)
		expected = 'no game courses found' if options else 'No game assets found.'
		assert expected in output, output
		assert '--assets PATH' in output, output
		assert 'Courses ()' not in output, output

	output = run('--assets', root / 'missing.ass', success=False)
	assert 'missing.ass' in output and '--assets PATH' in output, output
	corrupt = empty / 'corrupt.ass'
	corrupt.write_bytes(b'not an asset package')
	output = run('--assets', corrupt, success=False)
	assert 'no game courses found' not in output, output
	assert str(corrupt) in output and '--assets PATH' in output, output
	corrupt.unlink()

	assert '2 courses, start Harbor, 12 driver slots' in run()
	assert not config.exists(), 'startup must not create a config file'
	assert '2 courses, start Harbor' in run('--assets', root, cwd=root.parent)
	run('--config', 'server.cfg', success=False)
	run('--config', 'missing.cfg', success=False)

	config.write_text('MaxPlayers 4\nLaps 5\nStartTrack Ridge\nPortNumber 0\n')
	assert '2 courses, start Ridge, 4 driver slots' in run()
	process = subprocess.Popen([str(binary)], cwd=root, stdin=subprocess.PIPE,
		stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

	try:
		wait_banner(process)
		output, errors = process.communicate('quit\n', timeout=10)
		assert process.returncode == 0, output + errors
	finally:
		if process.poll() is None:
			process.kill()
			process.communicate()

	custom = root / 'custom'
	custom.mkdir()
	explicit = synthetic_config(custom)
	explicit.write_text('MaxPlayers 3\nTrack Custom fixture.mot\n')
	assert '1 courses, start Custom, 3 driver slots' in run('--config', explicit)
	assert '2 courses, start Ridge, 4 driver slots' in run('--config', config)

	config.write_text('')
	assert '2 courses, start Harbor, 12 driver slots' in run()
	config.write_text('Laps invalid\n')
	assert 'server.cfg:1:' in run(success=False)
	config.write_text('StartTrack Missing\n')
	assert 'not in the configured course list' in run(success=False)
	config.unlink()
	config.mkdir()
	run(success=False)
	config.rmdir()

	track(game / 'Track2.trk', 'harbor')
	assert 'duplicate server course name' in run(success=False)
	track(game / 'Track2.trk', 'NameTooLongForWire')
	run(success=False)
	track(game / 'Track2.trk', 'Ridge', 'missing.mot')
	run(success=False)
	track(game / 'Track2.trk', 'Ridge')
	(root / 'fixture.mot').write_bytes(b'invalid route')
	run(success=False)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('server', type=Path)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='openmh-server-startup-') as directory:
		check(args.server.resolve(), Path(directory))

	print('Optional config, asset discovery, overrides, live startup and failures passed.')


if __name__ == '__main__':
	main()
