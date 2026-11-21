#!/usr/bin/env python3
"""Exercise real operator input, locked settings, race chat and forced return."""

from tests.checks.support.build import build_executable
import argparse
import os
import re
from pathlib import Path
import select
import tempfile
import time

from tests.checks.live.check_dedicated_races import Client, LOBBY, RACING, wait
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.support.server import synthetic_course, start, wait_banner


class Console:
	def __init__(self, server):
		self.server = server
		self.pending = b''
		self.lines = []

	def poll(self):
		if select.select([self.server.stdout], [], [], 0)[0]:
			self.pending += os.read(self.server.stdout.fileno(), 8192)

			while b'\n' in self.pending:
				line, self.pending = self.pending.split(b'\n', 1)
				text = line.rstrip(b'\r').decode('latin1')
				match = re.fullmatch(r'\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] (.*)', text)
				assert match, ('missing console timestamp', text)
				self.lines.append(match[1])

	def send(self, text):
		self.server.stdin.write(text + '\n')
		self.server.stdin.flush()

	def check(self, clients, command, reply, settings):
		before = len(self.lines)
		self.send(command)

		def ready():
			self.poll()

			return reply in self.lines[before:] and all(c.settings == settings for c in clients)

		wait(self.server, clients, ready)


def qualify(args, config, root):
	server = start((build_executable(args.build, 'openmotorsv')).resolve(), config)
	clients = []

	try:
		try:
			_, port = wait_banner(server)
		except AssertionError:
			if server.poll() is not None:
				raise AssertionError(('server startup', server.communicate()))

			raise

		console = Console(server)

		for name in ('First', 'Second', 'Waiting'):
			clients.append(Client((build_executable(args.build, 'mh-dedicated-race-check')).resolve(),
				port, name, config, root))
			entered = (0xe06820, name + ' entered the lobby.')
			welcome = (0xe06820, "Type '/helpserver' for server commands and '/ready' to race!")

			# Admit in order and drain each welcome before counting later chat.
			# A roster update can arrive before its following join notices.
			wait(server, clients, lambda: all(entered in c.chat for c in clients)
				and welcome in clients[-1].chat)

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 14)
			and c.settings == (5, 0, 'Goldbridge') for c in clients))
		first, second, waiting = clients
		chats = [len(c.chat) for c in clients]

		for command, reply, settings in (
			('/track', 'Current track: Goldbridge', (5, 0, 'Goldbridge')),
			('/track Red', "Can't find track: Red", (5, 0, 'Goldbridge')),
			('/track REDROCK', 'Track changed to: Redrock', (5, 0, 'Redrock')),
			('/laps 0', 'Invalid number of laps!', (5, 0, 'Redrock')),
			('/laps 26', 'Invalid number of laps!', (5, 0, 'Redrock')),
			('/laps invalid', 'Invalid number of laps!', (5, 0, 'Redrock')),
			('/laps 2extra', 'Laps changed to: 2', (2, 0, 'Redrock')),
			('/laps', 'Current number of laps: 2', (2, 0, 'Redrock')),

			# The original console mode command does not publish a roster.
			('/gamemode elim', 'Gamemode changed to: elimination', (2, 0, 'Redrock')),
			('/gamemode', 'Current gamemode: Elimination', (2, 0, 'Redrock')),
			('/laps 2', 'Laps changed to: 2', (2, 1, 'Redrock')),
			('/gamemode norm', 'Gamemode changed to: normal', (2, 1, 'Redrock')),
			('/track Redrock', 'Track changed to: Redrock', (2, 0, 'Redrock'))):
			console.check(clients, command, reply, settings)

		assert all(len(c.chat) == n for c, n in zip(clients, chats)), 'operator settings leaked into chat'
		print('Console: original operator parsing, lock bypass and publication rules pass', flush=True)
		first.send('ready')
		second.send('ready')
		waiting.send('notready')
		wait(server, clients, lambda: first.state[1] == second.state[1] == RACING, 30)
		assert waiting.state[1] == LOBBY
		before = [len(c.chat) for c in clients]
		console.check(clients, '/track Goldbridge', 'console: /track Goldbridge', (2, 0, 'Redrock'))
		wait(server, clients, lambda: all((0xe06820, 'console: /track Goldbridge') in c.chat[n:]
			for c, n in zip(clients[:2], before[:2])))
		end = time.monotonic() + .3
		wait(server, clients, lambda: time.monotonic() >= end)
		assert len(waiting.chat) == before[2], waiting.chat
		console.send('/lobby')
		wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results
			and c.results[-1][3:5] == (0, 0) and len(c.rows.get(1, [])) == 12
			and c.settings == (2, 0, 'Goldbridge') for c in clients), 10)
		assert all(c.rows[1] == first.rows[1] for c in clients)
		print('Console: race-only chat and /lobby abort return all clients without awarding scores', flush=True)
		console.check(clients, '/gamemode elim', 'Gamemode changed to: elimination', (2, 0, 'Goldbridge'))
		first.send('ready')
		second.send('ready')
		wait(server, clients, lambda: first.state[1] == second.state[1] == RACING
			and first.settings == second.settings == (2, 1, 'Goldbridge'), 30)
		assert all(c.loads[-1][1:3] == (2, 'Goldbridge') for c in clients[:2])
		console.send('/lobby')
		wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results[-1][1] == 2
			and c.results[-1][3:5] == (0, 0) and len(c.rows.get(2, [])) == 12
			and c.settings == (2, 1, 'Redrock') for c in clients), 10)
		print('Console: clients can start and abort a second race with the operator-selected mode', flush=True)

		for client in clients:
			finish(client)

		console.send('/exit extra')
		output, errors = server.communicate(timeout=5)
		assert server.returncode == 0 and not errors, (output, errors)
	finally:
		for client in clients:
			client.stop()

		if server.poll() is None:
			server.terminate()

		server.communicate(timeout=5)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-console-') as temporary:
		root = Path(temporary)

		for name in ('goldbridge', 'redrock'):
			synthetic_course(root / (name + '.mot'))

		config = root / 'server.cfg'
		config.write_text('''SessionName Console
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
StepTrack
GameMode Normal
Laps 5
LockTrack
LockMode
LockLaps
''')
		qualify(args, config, root)

	print('Dedicated operator settings and race control passed through stdin and native UDP.')


if __name__ == '__main__':
	main()
