#!/usr/bin/env python3
"""Compile and exercise generated GLSL ES in an isolated Firefox WebGL 2 context.

This is a shader fixture, not the game's browser backend. Requires Firefox and
permission to bind a loopback HTTP server. Use an output/profile path visible to
the browser (snap Firefox cannot use the host's private /tmp namespace).
"""

import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path
import subprocess
import time

from tests.checks.render.check_shader_contracts import PAIRS, require


def page(build):
	root = build / 'shaders'

	if 'webgl' not in (root / 'targets.txt').read_text().splitlines():
		raise RuntimeError('Configure MH_SHADER_TARGETS with webgl and build mh_shaders first')

	names = {name + '_' + stage for pair in PAIRS
		for name, stage in zip(pair, ('vert', 'frag'))}
	sources = {name: (root / 'webgl' / (name + '.' + name.rsplit('_', 1)[1])).read_text()
		for name in sorted(names)}
	script = Path(__file__).with_name('shader_webgl_draw.js').read_text() + '\n'
	script += Path(__file__).with_name('shader_webgl.js').read_text()
	data = json.dumps(dict(sources=sources, pairs=PAIRS)).replace('<', '\\u003c')
	content = ('<!doctype html><meta charset="utf-8"><canvas id="canvas" width="64" '
		'height="64"></canvas><pre id="status">Running</pre><script>const fixture = '
		+ data + ';\n' + script + '</script>').encode()
	hashes = {name: hashlib.sha256(text.encode()).hexdigest() for name, text in sources.items()}

	return content, hashes


class Handler(BaseHTTPRequestHandler):
	def do_GET(self):
		self.send_response(200)
		self.send_header('Content-Type', 'text/html; charset=utf-8')
		self.send_header('Content-Length', str(len(self.server.page)))
		self.end_headers()
		self.wfile.write(self.server.page)

	def do_POST(self):
		self.server.report = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
		self.send_response(204)
		self.end_headers()

	def log_message(self, *args):
		pass


def uniform_members(reflection, members, prefix='', offset=0):
	result = {}

	for member in members:
		name = prefix + member['name']
		start = offset + member['offset']
		structure = reflection['types'].get(member['type'], {}).get('members')

		if structure and member.get('array'):
			for i in range(member['array'][0]):
				result.update(uniform_members(reflection, structure, f'{name}[{i}].',
					start + i * member['array_stride']))
		else:
			result[name] = (start, member.get('matrix_stride', 0))

	return result


def compare_layouts(report, contracts):
	for program in report['programs']:
		expected = {}

		for name, stage in zip(program['name'].split('/'), ('vert', 'frag')):
			reflection = json.loads((contracts / f'{name}_{stage}.json').read_text())

			for block in reflection.get('ubos', []):
				members = reflection['types'][block['type']]['members']
				expected[block['name']] = dict(size=block['block_size'],
					members=uniform_members(reflection, members))

		actual = {}

		for block in program['blocks']:
			fields = expected[block['name']]['members']
			members = {}

			for member in block['members']:
				# Browsers may prefix the block/instance name; retain nested array paths.
				names = [name for name in fields if member['name'] == name or
					member['name'].endswith('.' + name)]
				require(len(names) == 1, f"Unknown uniform {member['name']}")
				members[names[0]] = (member['offset'], member['matrixStride'])

			actual[block['name']] = dict(size=block['size'], members=members)

		require(actual == expected, f'{program["name"]}: browser uniform layout differs from reflection')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--output', type=Path, required=True, help='new output directory')
	parser.add_argument('--firefox', default='firefox')
	parser.add_argument('--contracts', type=Path,
		help='shader_contracts output directory for browser layout comparison')
	args = parser.parse_args()
	content, hashes = page(args.build.resolve())
	root = args.output.resolve()
	root.mkdir(parents=True, exist_ok=False)
	profile = root / 'profile'
	profile.mkdir()
	preferences = {'browser.shell.checkDefaultBrowser': False,
		'browser.aboutwelcome.enabled': False,
		'browser.startup.homepage_override.mstone': 'ignore',
		'browser.startup.page': 0, 'datareporting.policy.dataSubmissionEnabled': False}
	(profile / 'user.js').write_text(''.join(
		f'user_pref({json.dumps(k)}, {json.dumps(v)});\n' for k, v in preferences.items()))

	with HTTPServer(('127.0.0.1', 0), Handler) as server:
		server.page, server.report, server.timeout = content, None, 0.5
		url = f'http://127.0.0.1:{server.server_port}/'

		with (root / 'firefox.log').open('w') as log:
			process = subprocess.Popen([args.firefox, '--headless', '--no-remote',
				'--profile', str(profile), url], stdout=log, stderr=log)

			try:
				deadline = time.monotonic() + 90

				while server.report is None and time.monotonic() < deadline:
					if process.poll() is not None:
						raise RuntimeError(f'Firefox exited before reporting; see {root}/firefox.log')

					server.handle_request()

				if server.report is None:
					raise RuntimeError(f'Firefox did not report within 90 seconds; see {root}/firefox.log')
			finally:
				if process.poll() is None:
					process.terminate()

					try:
						process.wait(timeout=10)
					except subprocess.TimeoutExpired:
						process.kill()
						process.wait(timeout=10)

	server.report['shaders_sha256'] = hashes

	if server.report['passed'] and args.contracts:
		try:
			compare_layouts(server.report, args.contracts)
			server.report['layouts_match_reflection'] = True
		except RuntimeError as error:
			server.report.update(passed=False, error=str(error))

	(root / 'report.json').write_text(json.dumps(server.report, indent=2) + '\n')

	if not server.report['passed']:
		raise RuntimeError(server.report['error'])

	print(f'Firefox WebGL 2: {len(server.report["programs"])} linked programs and '
		f'{len(server.report["checks"])} pixel probes passed; report: {root}/report.json')


if __name__ == '__main__':
	main()
