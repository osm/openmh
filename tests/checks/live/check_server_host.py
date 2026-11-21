#!/usr/bin/env python3
"""Check the legacy headless lobby with synthetic courses and UDP clients."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import re
import select
import signal
import socket
import struct
import subprocess
import tempfile
import time


from tests.checks.support.server import legacy_config, start, wait_banner


from tests.checks.support.process import COMMAND_TIMEOUT


class Client:
	def __init__(self, public):
		self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		self.socket.bind(('127.0.0.1', 0))
		self.socket.setblocking(False)
		self.public = public
		self.remote = None
		self.rejected = None
		self.sequence = 2
		self.received = None
		self.messages = []
		self.slot = None
		self.answer_pings = True
		self.ping_count = 0
		self.status_count = 0
		self.socket.sendto(struct.pack('<HHI', 1, 8, 0x3fa66666), public)

	def poll(self):
		while True:
			try:
				data, source = self.socket.recvfrom(8192)
			except BlockingIOError:
				return

			if self.remote is None:
				kind, size = struct.unpack_from('<HH', data)
				assert len(data) == size

				if kind == 2:
					assert size == 20
					self.remote = source
				else:
					assert kind == 3 and source == self.public
					self.rejected = data[4:].rstrip(b'\0')

				continue

			assert source == self.remote

			if len(data) == 1:
				continue

			sequence = data[0]

			if sequence:
				self.socket.sendto(data[:1], source)

			if sequence == 0 or sequence != self.received:
				offset = 1

				while offset < len(data):
					kind, size = struct.unpack_from('<HH', data, offset)
					assert size >= 4 and offset + size <= len(data)
					message = data[offset:offset + size]
					self.messages.append(message)

					if kind == 10:
						assert sequence == 0 and size == 8
						self.ping_count += 1

						if self.answer_pings and self.slot is not None:
							pong = struct.pack('<HHB', 11, 9, self.slot) + message[4:]
							self.socket.sendto(b'\0' + pong, self.remote)
					elif kind == 15:
						assert sequence == 0 and size == 95
						self.status_count += 1
					elif kind == 7:
						assert sequence != 0, 'lobby chat must be reliable'

					offset += size

				if sequence:
					self.received = sequence

	def send(self, message):
		self.socket.sendto(bytes([self.sequence]) + message, self.remote)
		self.sequence = self.sequence % 255 + 1

	def latest(self, kind):
		return next((m for m in reversed(self.messages)
			if struct.unpack_from('<H', m)[0] == kind), None)


def pump(clients, ready, timeout=5):
	end = time.monotonic() + timeout

	while not ready():
		assert time.monotonic() < end, 'UDP fixture timed out'
		select.select([c.socket for c in clients], [], [], 0.02)

		for client in clients:
			client.poll()


def roster_count(message):
	if message is None:
		return -1

	assert len(message) == 1103 and message[22] == 13
	assert message[24] & 3 == 0, 'server was advertised as a driver'

	return sum(bool(message[24 + i * 83] & 3) for i in range(13))


def profile(slot, kind=4, name=b'Driver', rate=255):
	return struct.pack('<HHBBB16s5sB16s16s6IBBB', kind, 88, slot, 4, rate,
		name, b'TEAM', 1, b'car1', b'horn1',
		0x112233, 0x445566, 0x778899, 0xaabbcc,
		100, 200, 7, 9, 2)


def chat(slot, text, color=0x123456):
	return struct.pack('<HHBI', 7, len(text) + 10, slot, color) + text + b'\0'


def client_commands(clients):
	sender = clients[11]
	ordinary = chat(12, b'Driver: hello lobby')
	sender.send(ordinary)
	pump(clients, lambda: all(c.latest(7) == ordinary for c in clients[:11]))
	assert ordinary not in sender.messages, 'ordinary chat echoed to its author'

	for command, notice in ((b'/ReAdY ignored', b'Driver is ready.'),
		(b'/notready', b'Driver is not ready.'),
		(b'/notready', b'Driver will pass on this race.'),
		(b'/ready', b'Driver is not ready.')):
		sender.send(chat(12, b'Driver: ' + command))
		expected = chat(0, notice, 0xe06820)
		pump(clients, lambda: all(c.latest(7) == expected for c in clients))

		if command == b'/ReAdY ignored':
			pump(clients, lambda: all(c.latest(15) is not None and
				c.latest(15)[88] == 18 for c in clients))
			assert all(c.latest(15)[4] == 16 for c in clients)

	previous = [c.latest(7) for c in clients[:11]]
	sender.send(chat(12, b'Driver: /not-a-command'))
	expected = chat(0, b'Unknown command: /not-a-command', 0xe06820)
	pump(clients, lambda: sender.latest(7) == expected)
	assert [c.latest(7) for c in clients[:11]] == previous


def admit(clients, public, slot):
	client = Client(public)
	clients.append(client)
	pump(clients, lambda: client.remote is not None)
	client.send(struct.pack('<HHBI16s', 1, 25, 4, 0x400ae148, b''))
	pump(clients, lambda: client.latest(2) is not None)
	assert client.latest(2) == struct.pack('<HHB', 2, 5, slot)
	client.slot = slot
	client.send(profile(slot))
	pump(clients, lambda: roster_count(client.latest(6)) == slot)
	welcome = chat(0, b"Type '/helpserver' for server commands and '/ready' to race!", 0xe06820)
	pump(clients, lambda: client.latest(7) == welcome)

	return client


def live(binary, config):
	process = start(binary, config)
	clients = []

	try:
		public = wait_banner(process)

		for slot in range(1, 13):
			admit(clients, public, slot)

		pump(clients, lambda: all(roster_count(c.latest(6)) == 12 for c in clients))

		for client in clients:
			roster = client.latest(6)

			for slot in range(1, 13):
				offset = 24 + slot * 83
				assert roster[offset] == 2 and roster[offset + 1] == 30
				assert struct.unpack_from('<II', roster, offset + 72) == (0, 0)

		client_commands(clients)

		overflow = Client(public)
		clients.append(overflow)
		pump(clients, lambda: overflow.rejected is not None)
		assert overflow.rejected == b'Connection denied: Server full!'

		with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as query:
			query.settimeout(3)
			query.sendto(struct.pack('<HH', 4, 4), public)
			description, source = query.recvfrom(8192)
			assert source == public and len(description) == 1123
			assert description[20:] == clients[0].latest(6)

		clients[0].send(profile(1, kind=5, name=b'Edited', rate=0))
		pump(clients, lambda: all(c.latest(5) is not None for c in clients[1:12]))
		assert all(c.latest(5)[5:7] == bytes([6, 0]) for c in clients[1:12])
		assert clients[0].latest(5) is None

		# Release slot 12, then rejoin: transport and application ownership reset.
		departed = clients[11]
		departed.send(struct.pack('<HHB', 17, 5, 12))
		pump(clients, lambda: all(roster_count(c.latest(6)) == 11 for c in clients[:11]))
		replacement = admit(clients, public, 12)
		assert replacement.remote is not None

		# A valid second frame after a rejection must not use the closed peer.
		replacement.send(profile(1, kind=5) + struct.pack('<HHBI16s', 1, 25, 4, 0x400ae148, b''))
		pump(clients, lambda: all(roster_count(c.latest(6)) == 11 for c in clients[:11]))
		replacement = admit(clients, public, 12)
		replacement.send(chat(1, b'Edited: /notready'))
		pump(clients, lambda: all(roster_count(c.latest(6)) == 11 for c in clients[:11]))
		assert clients[0].latest(6)[24 + 83] == 6, 'foreign command changed driver flags'
		assert process.poll() is None
		process.stdin.write('status\nquit\n')
		process.stdin.flush()
		output, errors = process.communicate(timeout=5)
		assert process.returncode == 0 and not errors, (output, errors)
		assert '11/12 drivers' in output
	finally:
		for client in clients:
			client.socket.close()

		if process.poll() is None:
			process.terminate()
			process.communicate(timeout=5)


def background(binary, config):
	process = start(binary, config, ('--no-console',))

	try:
		process.stdin.close()
		process.stdin = None

		# Poll an actual child and allow the host loop to reach steady idle.
		time.sleep(0.15)
		assert process.poll() is None
		process.send_signal(signal.SIGTERM)
		output, errors = process.communicate(timeout=5)
		assert process.returncode == 0 and not output and not errors, (output, errors)
	finally:
		if process.poll() is None:
			process.kill()
			process.communicate(timeout=5)


def configured_limit(binary, config):
	process = start(binary, config)
	clients = []

	try:
		public = wait_banner(process)
		admit(clients, public, 1)
		admit(clients, public, 2)
		extra = Client(public)
		clients.append(extra)
		pump(clients, lambda: extra.remote is not None)
		extra.send(struct.pack('<HHBI16s', 1, 25, 4, 0x400ae148, b''))
		pump(clients, lambda: extra.latest(3) is not None)
		assert extra.latest(3) == struct.pack('<HHB', 3, 18, 1) + b'Server full!\0'

		with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as query:
			query.settimeout(3)
			query.sendto(struct.pack('<HH', 4, 4), public)
			description, _ = query.recvfrom(8192)
			assert len(description) == 1123 and description[42] == 3
			assert description[44] & 3 == 0

		process.send_signal(signal.SIGINT)
		output, errors = process.communicate(timeout=5)
		assert process.returncode == 0 and not errors, (output, errors)
	finally:
		for client in clients:
			client.socket.close()

		if process.poll() is None:
			process.terminate()
			process.communicate(timeout=5)


def heartbeat(binary, config):
	process = start(binary, config)
	clients = []

	try:
		public = wait_banner(process)
		active = admit(clients, public, 1)
		silent = admit(clients, public, 2)

		# Stay connected longer than the configured two-second timeout.
		pump(clients, lambda: all(c.status_count >= 3 for c in clients))

		for client in clients:
			status = client.latest(15)
			assert client.ping_count >= 3
			assert status[4] == 16, 'status must hide the server presence'
			assert status[11] == 2 and status[18] == 2
			assert struct.unpack_from('<I', status, 12)[0] < 2000
			assert struct.unpack_from('<I', status, 19)[0] < 2000

		silent.answer_pings = False
		silent_statuses = silent.status_count
		next_edit = time.monotonic()
		end = next_edit + 5
		edits = 0

		while roster_count(active.latest(6)) != 1:
			now = time.monotonic()
			assert now < end, 'silent client profile edits prevented timeout'

			if now >= next_edit:
				silent.send(profile(2, kind=5, name=b'No pong'))
				edits += 1
				next_edit = now + 0.2

			select.select([c.socket for c in clients], [], [], 0.02)

			for client in clients:
				client.poll()

		assert edits >= 5 and active.latest(5) is not None
		pump(clients, lambda: active.status_count >= silent_statuses + 3)
		assert active.latest(15)[11] == 2 and active.latest(15)[18] == 0
		assert process.poll() is None
		process.stdin.write('status\nquit\n')
		process.stdin.flush()
		output, errors = process.communicate(timeout=5)
		assert process.returncode == 0 and not errors, (output, errors)
		assert '1/12 drivers' in output
	finally:
		for client in clients:
			client.socket.close()

		if process.poll() is None:
			process.terminate()
			process.communicate(timeout=5)


def invalid_configs(binary, root):
	valid = (root / 'server.cfg').read_text()
	cases = [
		(re.sub(r'(?m)^PortNumber\s+\d+', 'PortNumber 65536', valid), '16 bits'),
		(valid + '\nStartTrack Missing\n', 'not in the configured course list'),
		(valid.replace('goldbridge.mot', 'missing.mot'), 'asset path component not found'),
		(valid + '\nMaxPlayers 2\nMinReady 3\n', 'MinReady cannot exceed MaxPlayers'),
		(valid + '\nMaxRate invalid\n', 'invalid')]

	for text, expected in cases:
		path = root / 'invalid.cfg'
		path.write_text(text)
		result = subprocess.run([str(binary), '--config', str(path), '--check'],
			capture_output=True, text=True, timeout=COMMAND_TIMEOUT)
		assert result.returncode == 1 and expected in result.stderr, result


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	args = parser.parse_args()
	binary = (build_executable(args.build, 'openmotorsv')).resolve()
	symbols = subprocess.run(['nm', '--defined-only', str(binary)],
		capture_output=True, text=True, check=True, timeout=COMMAND_TIMEOUT).stdout
	assert not re.search(r'\b(?:SDL_|enet_|mh_gpu_|mh_software_|mh_input_|mh_audio_|mh_menus_)', symbols)

	with tempfile.TemporaryDirectory(prefix='mh-server-host-') as temporary:
		root = Path(temporary)

		for name, players in (('server.cfg', 12), ('1on1.cfg', 2)):
			config = legacy_config(root, name, players)
			result = subprocess.run([str(binary), '--config', str(config), '--check'],
				capture_output=True, text=True, check=True, timeout=COMMAND_TIMEOUT)
			assert '16 courses' in result.stdout, result.stdout

		live(binary, root / 'server.cfg')
		configured_limit(binary, root / '1on1.cfg')
		config = root / 'heartbeat.cfg'
		text = (root / 'server.cfg').read_text()
		config.write_text(re.sub(r'(?m)^LobbyTimeOut\s+\d+', 'LobbyTimeOut 2', text))
		heartbeat(binary, config)
		background(binary, root / 'server.cfg')
		invalid_configs(binary, root)

	print('Headless host loads 16 synthetic courses; 12 UDP fixture clients join, '
		'receive rosters and edits; full rejection, discovery, leave/rejoin, '
		'chat/ready commands and ownership, unreliable ping/status, pong liveness, silent-client timeout, console '
		'and quiet signal shutdown pass.')


if __name__ == '__main__':
	main()
