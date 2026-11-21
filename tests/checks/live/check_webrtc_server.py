#!/usr/bin/env python3
"""Verify production server signaling with synthetic course data and raw HTTP."""

import argparse
from pathlib import Path
import socket

from tests.checks.support.build import build_executable
from tests.checks.support.server import start, synthetic_config, wait_browser_banner


ORIGIN = 'https://game.invalid'
SDP = ('v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n'
	'm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n'
	'a=ice-ufrag:test\r\na=ice-pwd:abcdefghijklmnopqrstuv\r\n'
	'a=fingerprint:sha-256 ' + ':'.join(f'{value:02X}' for value in range(32)) + '\r\n'
	'a=setup:actpass\r\na=mid:0\r\na=sctp-port:5000\r\n').encode()


def exchange(port, data):
	with socket.create_connection(('127.0.0.1', port), timeout=3) as connection:
		connection.sendall(data)
		response = bytearray()

		while True:
			part = connection.recv(4096)

			if not part:
				break

			response.extend(part)

			if len(response) > 20000:
				raise RuntimeError('Oversized signaling response')

	head, body = bytes(response).split(b'\r\n\r\n', 1)
	status = int(head.split(b' ', 2)[1])
	assert b'Connection: close' in head

	return status, head, body


def post(port, origin=ORIGIN, extra=b''):
	return exchange(port, b'POST / HTTP/1.1\r\nHost: game.invalid\r\n'
		+ f'Origin: {origin}\r\nContent-Type: application/sdp\r\nContent-Length: {len(SDP)}\r\n'.encode()
		+ extra + b'\r\n' + SDP)


def exercise(binary, directory, mode):
	directory.mkdir()
	config = synthetic_config(directory)

	with config.open('a') as file:
		file.write('Ban 2001:db8::bad\n')

	arguments = ['--web-address', '127.0.0.1', '--web-origin', ORIGIN,
		'--web-http-port', '0']

	if mode != 'direct':
		arguments += ['--web-proxy', '127.0.0.1' if mode == 'proxy' else '192.0.2.1']

	process = start(binary, config, arguments)
	startup = bytearray()

	try:
		ports, startup = wait_browser_banner(process)
		udp, http = ports['udp_port'], ports['http_port']
		assert udp == ports['native_port']
		preflight = (b'OPTIONS / HTTP/1.1\r\nHost: game.invalid\r\n'
			+ f'Origin: {ORIGIN}\r\n'.encode()
			+ b'Access-Control-Request-Method: POST\r\nAccess-Control-Request-Headers: content-type\r\n\r\n')
		status, head, body = exchange(http, preflight)
		assert status == 204 and not body and b'Content-Length:' not in head
		assert f'Access-Control-Allow-Origin: {ORIGIN}'.encode() in head
		assert post(http, 'https://untrusted.invalid')[0] == 403
		assert post(http, extra=b'Content-Length: 1\r\n')[0] == 400
		assert post(http, extra=b'Transfer-Encoding: chunked\r\n')[0] == 400
		trusted = b'X-Real-IP: 2001:db8::1234\r\n' if mode == 'proxy' else b''

		if mode == 'proxy':
			assert post(http)[0] == 400

		status, head, body = post(http, extra=trusted)
		assert status == 200 and b'Content-Type: application/sdp' in head
		assert b'a=ice-lite\r\n' in body and f'127.0.0.1 {udp} typ host'.encode() in body

		# A proxy supplies one canonical numeric client identity; another
		# remote cannot spoof it by sending the same forwarding header.
		forwarded_ban = b'X-Real-IP: 2001:0DB8:0:0:0:0:0:bad\r\n'
		assert post(http, extra=forwarded_ban)[0] == (403 if mode == 'proxy' else 200)
		statuses = [post(http, extra=trusted)[0] for _ in range(18)]
		assert 200 in statuses and 503 in statuses
		assert exchange(http, preflight)[0] == 204
	finally:
		output = errors = ''

		try:
			output, errors = process.communicate('quit\n', timeout=10)
		except Exception:
			process.kill()
			output, errors = process.communicate(timeout=5)

			raise
		finally:
			(directory / 'server.log').write_bytes(startup + output.encode() + errors.encode())

		assert process.returncode == 0, errors


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True)
	parser.add_argument('--output', type=Path, required=True)
	args = parser.parse_args()
	args.output.mkdir(parents=True, exist_ok=False)
	binary = build_executable(args.build, 'openmotorsv')

	for mode in ('direct', 'proxy', 'untrusted-proxy'):
		exercise(binary, args.output / mode, mode)

	print('Production signaling, CORS, admission bounds and trusted proxy identity passed')


if __name__ == '__main__':
	main()
