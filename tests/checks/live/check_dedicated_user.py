#!/usr/bin/env python3
"""Check detailed dedicated console player reports, live addresses and scores."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import re
import tempfile

from tests.checks.live.check_dedicated_console import Console
from tests.checks.live.check_dedicated_information import console_reply, settle
from tests.checks.live.check_dedicated_races import Client, LOBBY, RACING, wait
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.support.server import synthetic_course, start, wait_banner


def user(console, clients, player):
	console.poll()
	before = len(console.lines)
	chats = [len(c.chat) for c in clients]
	console.send(f'/user {player.state[0]}')

	def received():
		console.poll()

		return len(console.lines) >= before + 18

	wait(console.server, clients, received)
	settle(console.server, clients)
	console.poll()
	actual = console.lines[before:]
	races, score = player.scores[player.state[0]][:2]
	expected = ['', 'Name:       ' + player.name, 'Team:       TSL',
		'Car:        Kobra X3', 'Horn:       Honker1',
		'Gear:       Manual', 'Color:      2468ac', 'CarColor1:  000000',
		'CarColor2:  000000', 'CarColor3:  000000',
		f'Races:      {races}', f'Score:      {score}', 'Rate:       30',
		actual[13], 'Control:    Keyboard',
		f'Address:    127.0.0.1:{player.local_port}', 'Player in lobby.', '']
	assert actual == expected, (actual, expected)
	assert re.fullmatch(r'Ping:       [0-9]+', actual[13]), actual[13]
	assert player.local_port > 0
	assert all(len(c.chat) == n for c, n in zip(clients, chats)), 'console report leaked into chat'


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
				port, name, config, root, team='TSL'))

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 14)
			and c.local_port for c in clients))
		settle(server, clients)

		for client in clients:
			user(console, clients, client)

		for slot in (0, 4, 13, -1):
			console_reply(console, clients, f'/user {slot}', [f'No player with number: {slot}'])

		print('User: complete private console reports match profiles and each client UDP port; absent slots are rejected', flush=True)

		clients[2].send('notready')

		for client in clients[:2]:
			client.send('ready')

		wait(server, clients, lambda: all(c.state[1] == RACING for c in clients[:2]), 30)
		wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results
			for c in clients), 90)
		settle(server, clients)
		assert all(c.scores[c.state[0]][0] == 1 and c.scores[c.state[0]][1] > 0
			for c in clients[:2])
		assert clients[2].scores[clients[2].state[0]][:2] == (0, 0)

		for client in clients:
			user(console, clients, client)

		departed = clients[-1]
		slot = departed.state[0]
		finish(departed)
		clients.pop()
		wait(server, clients, lambda: all(not (c.state[2] & (1 << slot)) for c in clients))
		settle(server, clients)
		console_reply(console, clients, f'/user {slot}', [f'No player with number: {slot}'])
		print('User: completed-race totals match client scores; passing and departed players retain correct handling', flush=True)

		for client in clients:
			finish(client)

		console.send('/quit')
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

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-user-') as temporary:
		root = Path(temporary)
		synthetic_course(root / 'goldbridge.mot')
		config = root / 'server.cfg'
		config.write_text('''SessionName User
ServerName Console
NetworkProtocol legacy
PortNumber 0
MaxPlayers 12
MaxRate 30
MinReady 1
AllReady
StartTrack Goldbridge
Track Goldbridge goldbridge.mot
GameMode Normal
Laps 1
''')
		qualify(args, config, root)

	print('Dedicated detailed user reports passed through the real host and clients.')


if __name__ == '__main__':
	main()
