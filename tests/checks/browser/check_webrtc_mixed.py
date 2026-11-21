#!/usr/bin/env python3
"""Exercise one native and one browser game session on the production server."""

import argparse
import ipaddress
import json
import os
from pathlib import Path
import select
import subprocess
import time

from tests.checks.support.browser import run_browser
from tests.checks.support.build import build_executable
from tests.checks.support.server import start, synthetic_config, wait_browser_banner


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True)
	parser.add_argument('--wasm-build', type=Path, required=True)
	parser.add_argument('--output', type=Path, required=True)
	parser.add_argument('--browser', choices=('chrome', 'firefox'), required=True)
	parser.add_argument('--executable')
	parser.add_argument('--address', type=ipaddress.IPv4Address, default='127.0.0.1',
		help='UDP bind and advertised address; use a LAN address for Firefox ICE')
	args = parser.parse_args()
	server_binary = build_executable(args.build, 'openmotorsv')
	native_binary = build_executable(args.build, 'test-mixed-native-client')
	browser_binary = build_executable(args.wasm_build, 'test-mixed-browser-client')
	page = ('<!doctype html><meta charset="utf-8">'
		f'<script src="/{browser_binary.name}"></script><script src="/check.js"></script>')
	resources = {
		'/': (page.encode(), 'text/html'),
		'/' + browser_binary.name: (browser_binary.read_bytes(), 'text/javascript'),
		'/' + browser_binary.with_suffix('.wasm').name: (browser_binary.with_suffix('.wasm').read_bytes(), 'application/wasm'),
		'/check.js': (Path(__file__).with_name('webrtc_mixed.js').read_bytes(), 'text/javascript'),
	}
	server = native = None
	startup = bytearray()
	native_output = bytearray()

	def request(message):
		nonlocal server, native, startup

		if message.get('start'):
			if server is not None:
				raise RuntimeError('Server already started')

			directory = args.output / 'game'
			directory.mkdir()
			config = synthetic_config(directory)

			with config.open('a') as file:
				file.write('MinReady 2\nAllReady 1\nLobbyTimeOut 30\nStartTimeOut 30\n')

			extra = ['--bind', str(args.address), '--web-address', str(args.address),
				'--web-origin', message['origin']]
			server = start(server_binary, config, extra)
			ports, startup = wait_browser_banner(server, str(args.address))
			assert ports["native_port"] == ports["udp_port"] == ports["http_port"]
			native = subprocess.Popen([str(native_binary), str(args.address), str(ports['native_port'])],
				stdout=subprocess.PIPE, stderr=subprocess.PIPE)
			resources['/config'] = (json.dumps({'endpoint': f'http://127.0.0.1:{ports["http_port"]}/'}).encode(),
				'application/json')

			return

		if not message.get('verify') or native is None:
			raise RuntimeError('No native session to verify')

		deadline = time.monotonic() + 10

		while b'CLIENT VERIFIED' not in native_output:
			remaining = deadline - time.monotonic()

			if remaining <= 0 or not select.select([native.stdout], [], [], remaining)[0]:
				raise RuntimeError('Native session did not finish the mixed race')

			chunk = os.read(native.stdout.fileno(), 4096)

			if not chunk:
				raise RuntimeError('Native session exited before verification')

			native_output.extend(chunk)

			if len(native_output) > 16384:
				raise RuntimeError('Unexpected native session output')

		native.terminate()
		output, errors = native.communicate(timeout=5)
		(args.output / 'native.log').write_bytes(native_output + output + errors)

		if native.returncode:
			raise RuntimeError('Native session failed: ' + errors.decode(errors='replace'))

	try:
		report = run_browser(resources, args.output, args.browser, args.executable,
			timeout=50, input_handler=request)
	finally:
		if native is not None and not (args.output / 'native.log').exists():
			if native.poll() is None:
				native.terminate()

			try:
				output, errors = native.communicate(timeout=5)
			except subprocess.TimeoutExpired:
				native.kill()
				output, errors = native.communicate(timeout=5)

			(args.output / 'native.log').write_bytes(native_output + output + errors)

		if server is not None:
			try:
				output, errors = server.communicate('quit\n', timeout=10)
			except subprocess.TimeoutExpired:
				server.kill()
				output, errors = server.communicate(timeout=5)

			(args.output / 'server.log').write_bytes(startup + output.encode() + errors.encode())

	if not report['passed']:
		raise RuntimeError(report['error'])

	if server.returncode:
		raise RuntimeError('Server failed; see server.log')

	print(f'{args.browser}: native/browser lobby, race snapshots and return passed on openmotorsv')


if __name__ == '__main__':
	main()
