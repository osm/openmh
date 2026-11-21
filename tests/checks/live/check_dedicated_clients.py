#!/usr/bin/env python3
"""Run twelve native client processes against the headless dedicated lobby.

Clients use production network sessions, UDP transport, command dispatch and
lobby view composition. These integration processes run without a graphical
window or race.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import select
import subprocess
import tempfile
import time

from tests.checks.support.server import legacy_config, start, wait_banner


class Client:
	def __init__(self, binary, port, name):
		self.process = subprocess.Popen([str(binary), str(port), name],
			stdin=subprocess.PIPE,
			stdout=subprocess.PIPE,
			stderr=subprocess.PIPE)
		self.name = name
		self.pending = b''
		self.state = None
		self.chat = []

	def read(self):
		data = os.read(self.process.stdout.fileno(), 8192)
		assert data, f'{self.name}: unexpected output EOF'
		self.pending += data

		while b'\n' in self.pending:
			line, self.pending = self.pending.split(b'\n', 1)
			line = line.rstrip(b'\r')

			if line.startswith(b'STATE '):
				self.state = tuple(map(int, line.split()[1:]))
				assert len(self.state) == 6, line
			elif line.startswith(b'CHAT '):
				self.chat.append(line[5:].decode('latin1'))
			else:
				raise AssertionError(line)

	def send(self, command):
		self.process.stdin.write(command.encode('latin1') + b'\n')
		self.process.stdin.flush()

	def stop(self):
		if self.process.poll() is None:
			self.process.terminate()

		self.process.communicate(timeout=5)


def wait(clients, ready, timeout=8):
	end = time.monotonic() + timeout

	while not ready():
		for client in clients:
			if client.process.poll() is not None:
				_, errors = client.process.communicate()

				raise AssertionError((client.name, client.process.returncode, errors))

		assert time.monotonic() < end, [(c.name, c.state, c.chat[-2:]) for c in clients]
		readable, _, _ = select.select([c.process.stdout for c in clients], [], [], 0.02)

		for client in clients:
			if client.process.stdout in readable:
				client.read()


def qualify(server_binary, client_binary, config):
	server = start(server_binary, config)
	clients, owned = [], []

	try:
		_, port = wait_banner(server)

		for i in range(12):
			client = Client(client_binary, port, f'Native{i + 1:02}')
			clients.append(client)
			owned.append(client)

		wait(clients, lambda: all(c.state is not None and c.state[1] == 12
			for c in clients))
		assert sorted(c.state[0] for c in clients) == list(range(1, 13))
		last = next(c for c in clients if c.state[0] == 12)

		# Thirteen real client processes: the extra one receives Server full.
		extra = subprocess.run([str(client_binary), str(port), 'Overflow'],
			stdin=subprocess.DEVNULL, capture_output=True,
			timeout=8)
		assert extra.returncode == 1 and b'Server full!' in extra.stderr, extra

		# Keep slot one uncommitted so this lobby regression does not start a race.
		# Full lifecycle qualification has its own coordinator/client tests.
		for client in clients:
			if client.state[0] != 1:
				client.send('/ready')

		wait(clients, lambda: all(c.state[2] == 8189 for c in clients))
		wait(clients, lambda: all(c.state[4] >= 3 for c in clients))

		last.send('/rate 27')
		wait(clients, lambda: all(c.state[5] == 27 for c in clients))
		assert all(c.state[2] == 8189 for c in clients), 'profile edit erased ready'

		for command, ready, passing, suffix in (
			('/notready', 4093, 0, ' is not ready.'),
			('/notready', 4093, 4096, ' will pass on this race.'),
			('/ready', 4093, 0, ' is not ready.'),
			('/ready', 8189, 0, ' is ready.')):
			before = [len(c.chat) for c in clients]
			last.send(command)
			wait(clients, lambda: all(c.state[2:4] == (ready, passing)
				and last.name + suffix in c.chat[n:]
				for c, n in zip(clients, before)))

		before = [len(c.chat) for c in clients]
		last.send('hello dedicated lobby')
		text = last.name + ': hello dedicated lobby'
		wait(clients, lambda: all(text in c.chat[n:]
			for c, n in zip(clients, before)))
		assert all(c.chat[n:].count(text) == 1 for c, n in zip(clients, before))
		last.send('/unknown-dedicated-command')
		reply = 'Unknown command: /unknown-dedicated-command'
		wait(clients, lambda: reply in last.chat)
		assert all(reply not in c.chat for c in clients if c is not last)
		assert all(not any(t.startswith(last.name + ': /') for t in c.chat)
			for c in clients), 'command was locally echoed'

		clients.remove(last)
		last.send('/quit')
		output, errors = last.process.communicate(timeout=5)
		assert last.process.returncode == 0 and not errors, (output, errors)
		wait(clients, lambda: all(c.state[1] == 11 for c in clients))
		replacement = Client(client_binary, port, 'Replacement')
		clients.append(replacement)
		owned.append(replacement)
		wait(clients, lambda: all(c.state is not None and c.state[1] == 12
			for c in clients))
		assert replacement.state[0] == 12
		wait(clients, lambda: all(c.state[2:4] == (4093, 0) for c in clients))
		replacement.send('/ready')
		wait(clients, lambda: all(c.state[2] == 8189 for c in clients))

		for client in clients:
			client.send('/quit')

		for client in clients:
			output, errors = client.process.communicate(timeout=5)
			assert client.process.returncode == 0 and not errors, (output, errors)

		server.stdin.write('quit\n')
		server.stdin.flush()
		output, errors = server.communicate(timeout=5)
		assert server.returncode == 0 and not errors, (output, errors)
	finally:
		for client in owned:
			client.stop()

		if server.poll() is None:
			server.terminate()

		server.communicate(timeout=5)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-clients-') as temporary:
		root = Path(temporary)
		config = legacy_config(root)
		text = config.read_text().replace('LobbyTimeOut 10', 'LobbyTimeOut 2')
		config.write_text(text)
		qualify((build_executable(args.build, 'openmotorsv')).resolve(),
			(build_executable(args.build, 'mh-dedicated-client-check')).resolve(), config)

	print('Twelve separate native clients: admission, hidden server, lobby view, '
		'heartbeats beyond timeout, ready/pass commands, profile edits, chat, '
		'full rejection and slot-12 leave/rejoin pass. '
		'Dedicated racing and original-client interoperability are separate checks.')


if __name__ == '__main__':
	main()
