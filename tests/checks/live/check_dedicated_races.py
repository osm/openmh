#!/usr/bin/env python3
"""Qualify real dedicated UDP races with twelve independent native processes.

Uses the production server, transport, session and synthetic closed-loop courses.
Car poses follow the course under controlled motion, not rendered/physics-driven
play. Original-peer interoperability is not claimed.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import select
import subprocess
import tempfile
import time

from tests.checks.support.server import synthetic_course, start, wait_banner

LOBBY, RACING = 2, 7
DRIVERS = (1 << 13) - 2
POINTS = [12, 10, 8, 6, 4, 3, 2, 1, 0, 0, 0, 0]


class Client:
	def __init__(self, binary, port, name, config, root, team=None, password=None):
		self.name = name
		options = [] if team is None and password is None else [team or '']

		if password is not None:
			options.append(password)

		self.process = subprocess.Popen([str(binary), str(port), name,
			str(config), str(root)] + options,
			stdin=subprocess.PIPE, stdout=subprocess.PIPE,
			stderr=subprocess.PIPE)
		self.pending = b''
		self.state = None
		self.loads = []
		self.grids = []
		self.results = []
		self.rows = {}
		self.chat = []
		self.settings = None
		self.scores = {}
		self.server = None
		self.local_port = 0

	def read(self):
		data = os.read(self.process.stdout.fileno(), 16384)
		assert data, (self.name, 'unexpected EOF', self.process.poll(),
			self.process.stderr.read() if self.process.poll() is not None else b'')
		self.pending += data

		while b'\n' in self.pending:
			line, self.pending = self.pending.split(b'\n', 1)
			line = line.rstrip(b'\r')

			if line.startswith(b'PORT '):
				self.local_port = int(line.split()[1])
				continue

			if line.startswith(b'SERVER '):
				_, flags, status, name = line.decode('latin1').split(' ', 3)
				self.server = (int(flags), int(status), name)
				continue

			if line.startswith(b'CHAT '):
				_, color, message = line.decode('latin1').split(' ', 2)
				self.chat.append((int(color), message))
				continue

			fields = line.decode('latin1').split()
			kind, values = fields[0], fields[1:]

			if kind == 'STATE':
				self.state = tuple(map(int, values))
				assert len(self.state) == 6
			elif kind == 'SETTINGS':
				self.settings = (int(values[0]), int(values[1]), values[2])
			elif kind == 'SCORE':
				self.scores[int(values[0])] = tuple(map(int, values[1:]))
			elif kind == 'GRID':
				self.grids.append(tuple(map(int, values)))
			elif kind == 'LOAD':
				self.loads.append((int(values[0]), int(values[1]), values[2], int(values[3])))
			elif kind == 'RESULT':
				self.results.append((*map(int, values[:5]), values[5]))
			elif kind == 'ROW':
				revision, position, speed, lap, race = map(int, values[:5])
				self.rows.setdefault(revision, []).append((position, speed, lap, race,
					values[5] if len(values) > 5 else ''))
			else:
				raise AssertionError((self.name, line))

	def send(self, command):
		self.process.stdin.write(command.encode('ascii') + b'\n')
		self.process.stdin.flush()

	def stop(self):
		if self.process.poll() is None:
			self.process.terminate()

		self.process.communicate(timeout=5)


def wait(server, clients, predicate, timeout=10, relays=()):
	end = time.monotonic() + timeout

	while not predicate():
		for relay in relays:
			relay.check()

		if server.poll() is not None:
			raise AssertionError(('server exited', server.returncode, server.communicate()))

		for client in clients:
			if client.process.poll() is not None:
				raise AssertionError((client.name, client.process.returncode,
					client.process.communicate()))

		assert time.monotonic() < end, [(c.name, c.state, c.loads, c.results) for c in clients]
		streams = [c.process.stdout for c in clients]
		readable, _, _ = select.select(streams, [], [], .02)

		for client in clients:
			if client.process.stdout in readable:
				client.read()


def check_results(clients, mode, round_number, faults):
	all_names = {c.name for c in clients}
	common = None

	for client in clients:
		player, revision, seen, races, points, track = client.results[-1]
		assert revision == races == round_number, (client.name, client.results)
		assert seen == DRIVERS & ~(1 << player), (client.name, seen)
		assert track == ('Redrock' if round_number == 1 else 'Goldbridge')
		rows = client.rows[revision]
		assert len(rows) == 12 and {r[4] for r in rows} == all_names, rows
		assert sorted(r[0] for r in rows) == list(range(1, 13)), rows

		if mode == 'Normal':
			finishes = [r[2] > 0 and r[3] > 0 for r in rows]
			assert (any(finishes) if faults else all(finishes)), rows

		assert all(r[1] >= 0 for r in rows)
		own = next(r for r in rows if r[4] == client.name)
		previous = 0 if round_number == 1 else client.results[-2][4]
		assert points == previous + POINTS[own[0] - 1], (client.name, points, previous, own)

		if common is None:
			common = rows

		assert rows == common, 'clients disagree about authoritative results'
		assert client.loads[-1][1:3] == (round_number,
			'Goldbridge' if round_number == 1 else 'Redrock')


def qualify(args, config, root, mode):
	server = start((build_executable(args.build, 'openmotorsv')).resolve(), config)
	clients, relays = [], []

	try:
		try:
			_, port = wait_banner(server)
		except AssertionError:
			if server.poll() is not None:
				raise AssertionError(('server startup', server.communicate()))

			raise

		for i in range(12):
			client_port = port

			if args.faults:
				from tests.checks.support.udp_faults import DatagramRelay

				relay = DatagramRelay(port)
				relay.__enter__()
				relays.append(relay)
				client_port = relay.port

			clients.append(Client((build_executable(args.build, 'mh-dedicated-race-check')).resolve(),
				client_port, f'Driver{i + 1:02}', config, root))

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, DRIVERS)
			for c in clients), relays=relays)
		assert sorted(c.state[0] for c in clients) == list(range(1, 13))

		for round_number in (1, 2):
			for client in clients:
				client.send('ready')

			wait(server, clients, lambda: all(c.state[1] == RACING for c in clients), 30, relays)
			print(f'{mode}: twelve clients started race {round_number}', flush=True)
			wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results
				and c.results[-1][1] == round_number
				and len(c.rows.get(round_number, [])) == 12 for c in clients), 100, relays)
			check_results(clients, mode, round_number, args.faults)
			print(f'{mode}: race {round_number} results, scores, relay and rotation pass', flush=True)

		for client in clients:
			client.send('quit')

		for client in clients:
			output, errors = client.process.communicate(timeout=5)
			assert client.process.returncode == 0 and not errors, (client.name, output, errors)

		for relay in relays:
			relay.check()

			for counters in relay.stats:
				assert all(counters[k] > 0 for k in ('dropped', 'duplicated', 'delayed', 'reordered')), counters

		if relays:
			print(f'{mode}: all twelve links exercised loss, delay, duplication and reordering in both directions', flush=True)

		server.stdin.write('quit\n')
		server.stdin.flush()
		output, errors = server.communicate(timeout=5)
		assert server.returncode == 0 and not errors, (output, errors)
	finally:
		for client in clients:
			client.stop()

		if server.poll() is None:
			server.terminate()

		server.communicate(timeout=5)

		for relay in relays:
			relay.__exit__()


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--faults', action='store_true', help='perturb every client UDP link in both directions')
	parser.add_argument('--mode', choices=('Normal', 'Elimination', 'both'), default='both')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-races-') as temporary:
		root = Path(temporary)

		for name in ('goldbridge.mot', 'redrock.mot'):
			synthetic_course(root / name)

		for mode in ('Normal', 'Elimination') if args.mode == 'both' else (args.mode,):
			config = root / 'server.cfg'
			config.write_text(f'''SessionName RaceCheck
ServerName Console
NetworkProtocol legacy
PortNumber 0
MaxPlayers 12
MaxRate 30
MinReady 1
AllReady
StartTrack Goldbridge
Track Goldbridge goldbridge.mot
Track Redrock redrock.mot
GameMode {mode}
Grid ScoreNormal
Laps 1
EndCountdown 10
MaxRaceTime 3
StepTrack
''')
			qualify(args, config, root, mode)

	print('Dedicated races: twelve separate native UDP clients, two races per selected mode, '
		'synthetic courses, peer poses, results, scoring and track rotation passed. '
		'Rendered driving, recording and original-peer checks remain separate.')


if __name__ == '__main__':
	main()
