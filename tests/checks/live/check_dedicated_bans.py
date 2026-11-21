#!/usr/bin/env python3
"""Check configured dedicated IP bans at the original UDP handshake boundary."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import socket
import struct
import subprocess
import tempfile

from tests.checks.support.server import synthetic_course, start, wait_banner

HELLO = struct.pack('<HHI', 1, 8, 0x3fa66666)
BANNED = b"Connection denied: You're banned!\0"


def exchange(peer, target, packet):
	peer.sendto(packet, target)
	data, source = peer.recvfrom(8192)
	kind, size = struct.unpack_from('<HH', data)
	assert size == len(data), data

	return kind, data[4:], source


def rejected(binary, port, config, root):
	process = subprocess.Popen([str(binary), str(port), 'Blocked', str(config), str(root)],
		stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

	try:
		process.wait(timeout=10)
		output, errors = process.communicate()
		assert process.returncode == 1 and errors == BANNED[:-1] + b'\n', (output, errors)
	finally:
		if process.poll() is None:
			process.terminate()

		process.communicate(timeout=5)


def qualify(args, config, root):
	server = start((build_executable(args.build, 'openmotorsv')).resolve(), config)
	sockets = []

	try:
		try:
			_, port = wait_banner(server)
		except AssertionError:
			if server.poll() is not None:
				raise AssertionError(('server startup', server.communicate()))

			raise

		target = ('127.0.0.1', port)
		blocked = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		sockets.append(blocked)
		blocked.bind(('127.0.0.1', 0))
		blocked.settimeout(5)
		kind, text, source = exchange(blocked, target, struct.pack('<HHI', 1, 8, 0x3f800000))
		assert kind == 3 and source == target and text == (
			b'Connection denied: Wrong mhsock version, Server v1.30, Client v1.00.\0'), text

		for _ in range(16):
			assert exchange(blocked, target, HELLO) == (3, BANNED, target)

		kind, _, source = exchange(blocked, target, struct.pack('<HH', 4, 4))
		assert kind == 5 and source == target, (kind, source)
		rejected((build_executable(args.build, 'mh-dedicated-race-check')).resolve(), port, config, root)
		print('Bans: version rejection precedes IP filtering; repeated banned hellos and native client receive the original rejection; discovery remains available', flush=True)

		# These endpoints occupy the transport without sending a game join.
		# Exact IP matching must not reject the neighbouring loopback address.
		for _ in range(12):
			peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
			sockets.append(peer)
			peer.bind(('127.0.0.2', 0))
			peer.settimeout(5)
			kind, _, source = exchange(peer, target, HELLO)
			assert kind == 2 and source[0] == target[0], (kind, source)

		assert exchange(blocked, target, HELLO) == (3, BANNED, target)
		extra = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		sockets.append(extra)
		extra.bind(('127.0.0.2', 0))
		extra.settimeout(5)
		assert exchange(extra, target, HELLO) == (3, b'Connection denied: Server full!\0', target)
		print('Bans: rejected addresses consume no endpoints; twelve other-address endpoints fit, and banned rejection precedes full capacity', flush=True)
		server.stdin.write('/quit\n')
		server.stdin.flush()
		output, errors = server.communicate(timeout=5)
		assert server.returncode == 0 and not errors, (output, errors)
	finally:
		for peer in sockets:
			peer.close()

		if server.poll() is None:
			server.terminate()

		server.communicate(timeout=5)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-bans-') as temporary:
		root = Path(temporary)
		synthetic_course(root / 'goldbridge.mot')
		config = root / 'server.cfg'
		config.write_text('''SessionName Bans
ServerName Console
NetworkProtocol legacy
PortNumber 0
MaxPlayers 12
MinReady 12
AllReady
Password Secret
Ban 192.0.2.1
Ban 127.0.0.1
StartTrack Goldbridge
Track Goldbridge goldbridge.mot
''')
		qualify(args, config, root)

	print('Dedicated configured IP bans passed through the real server and native client.')


if __name__ == '__main__':
	main()
