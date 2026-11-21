#!/usr/bin/env python3
"""Check the production browser transport against the native WebRTC provider."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
import os
from pathlib import Path
import select
import subprocess
import threading
import time

from tests.checks.support.browser import run_browser
from tests.checks.support.build import build_executable


class Signaling(BaseHTTPRequestHandler):
	def do_OPTIONS(self):
		self.send_response(204)
		self.send_header('Access-Control-Allow-Origin', '*')
		self.send_header('Access-Control-Allow-Methods', 'POST')
		self.send_header('Access-Control-Allow-Headers', 'Content-Type')
		self.end_headers()

	def do_POST(self):
		self.connection.settimeout(10)

		try:
			length = int(self.headers.get('Content-Length', '0'))

			if self.path not in ('/0', '/1') or not 0 < length <= 16384:
				raise ValueError('Invalid signaling request')

			offer = self.rfile.read(length)

			if len(offer) != length:
				raise ValueError('Incomplete signaling offer')

			reply = self.server.exchange(int(self.path[1:]), offer)
		except Exception as error:
			self.send_error(400, str(error))

			return

		self.send_response(200)
		self.send_header('Access-Control-Allow-Origin', '*')
		self.send_header('Content-Type', 'application/sdp')
		self.send_header('Content-Length', str(len(reply)))
		self.end_headers()
		self.wfile.write(reply)

	def log_message(self, *args):
		pass


class NativePeer:
	def __init__(self, binary, address, output):
		self.command = [str(binary), str(address)]
		self.output = output
		self.condition = threading.Condition()
		self.offers = {}
		self.answers = None
		self.error = None
		self.process = None
		self.closing = False

	def exchange(self, index, offer):
		with self.condition:
			if self.closing:
				raise RuntimeError('Signaling fixture is closing')

			if index in self.offers:
				self.error = 'Canceled connection submitted an extra offer'
				self.condition.notify_all()

				raise RuntimeError(self.error)

			self.offers[index] = offer

			if len(self.offers) == 2:
				try:
					self.start()
				except Exception as error:
					self.error = str(error)

				self.condition.notify_all()

			if not self.condition.wait_for(lambda: self.answers is not None or self.error, 10):
				raise RuntimeError('Second browser offer did not arrive')

			if self.error:
				raise RuntimeError(self.error)

			return self.answers[index]

	def start(self):
		self.process = subprocess.Popen(self.command, stdin=subprocess.PIPE,
			stdout=subprocess.PIPE, stderr=subprocess.PIPE)
		self.process.stdin.write(self.offers[0] + b'\0' + self.offers[1])
		self.process.stdin.close()
		self.process.stdin = None
		reply = bytearray()
		deadline = time.monotonic() + 10
		marker = b'--READY--\n'

		while marker not in reply:
			remaining = deadline - time.monotonic()

			if remaining <= 0 or not select.select([self.process.stdout], [], [], remaining)[0]:
				raise RuntimeError('Native SDP answer timed out')

			chunk = os.read(self.process.stdout.fileno(), 4096)

			if not chunk:
				raise RuntimeError('Native peer exited before producing SDP')

			reply.extend(chunk)

			if len(reply) > 8192:
				raise RuntimeError('Native answer too large')

		self.answers = bytes(reply).split(marker)[0].split(b'\0')

		if len(self.answers) != 2:
			raise RuntimeError('Unexpected native answer count')

	def verify(self, message):
		if not message.get('verify') or self.process is None or self.error:
			raise RuntimeError(self.error or 'No native peer to verify')

		output, errors = self.process.communicate(timeout=10)
		(self.output / 'native.log').write_bytes(output + errors)

		if self.process.returncode or b'PROVIDER VERIFIED' not in output:
			raise RuntimeError(f'Native peer failed: {errors.decode(errors="replace")}')

	def close(self):
		# Wait for bounded answer construction before touching its pipes, and
		# prevent a delayed request handler from starting a new child afterward.
		with self.condition:
			self.closing = True
			self.error = self.error or 'Signaling fixture is closing'
			self.condition.notify_all()

		if self.process is None:
			return

		if self.process.poll() is None:
			self.process.terminate()

			try:
				self.process.wait(timeout=5)
			except subprocess.TimeoutExpired:
				self.process.kill()
				self.process.wait(timeout=5)

		if not (self.output / 'native.log').exists():
			output, errors = self.process.communicate(timeout=5)
			(self.output / 'native.log').write_bytes(output + errors)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True, help='Native build')
	parser.add_argument('--wasm-build', type=Path, required=True)
	parser.add_argument('--output', type=Path, required=True)
	parser.add_argument('--browser', choices=('chrome', 'firefox'), required=True)
	parser.add_argument('--executable')
	parser.add_argument('--address', type=ipaddress.IPv4Address, default='127.0.0.1')
	args = parser.parse_args()
	native = NativePeer(build_executable(args.build, 'test-webrtc-provider-peer'), args.address, args.output)
	binary = build_executable(args.wasm_build, 'test-webrtc-browser-transport')
	page = ('<!doctype html><meta charset="utf-8">'
		f'<script src="/{binary.name}"></script><script src="/check.js"></script>')
	resources = {
		'/': (page.encode(), 'text/html'),
		'/' + binary.name: (binary.read_bytes(), 'text/javascript'),
		'/' + binary.with_suffix('.wasm').name: (binary.with_suffix('.wasm').read_bytes(), 'application/wasm'),
		'/check.js': (Path(__file__).with_name('webrtc_transport.js').read_bytes(), 'text/javascript'),
	}

	with ThreadingHTTPServer(('127.0.0.1', 0), Signaling) as signaling:
		signaling.exchange = native.exchange
		resources['/config'] = (json.dumps({'signaling': f'http://127.0.0.1:{signaling.server_port}'}).encode(),
			'application/json')
		worker = threading.Thread(target=signaling.serve_forever, daemon=True)
		worker.start()

		try:
			report = run_browser(resources, args.output, args.browser, args.executable,
				timeout=40, input_handler=native.verify)
		finally:
			signaling.shutdown()
			worker.join(timeout=5)
			native.close()

	if not report['passed']:
		raise RuntimeError(report['error'])

	print(f'{args.browser}: production Wasm/browser and native transports exchanged game messages')


if __name__ == '__main__':
	main()
