#!/usr/bin/env python3
"""Qualify track/mode/grid commands and locks through real native UDP races."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import tempfile
import time

from tests.checks.live.check_dedicated_races import Client, LOBBY, RACING, wait
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.support.server import synthetic_course, start, wait_banner

COLOR = 0xe06820


def change(server, clients, sender, command, settings, reply, private=False):
	before = [len(c.chat) for c in clients]
	sender.send('chat ' + command)
	wait(server, clients, lambda: all(c.settings == settings
		and ((COLOR, reply) in c.chat[n:] if not private or c is sender else True)
		for c, n in zip(clients, before)))
	deadline = time.monotonic() + .2
	wait(server, clients, lambda: time.monotonic() >= deadline)

	for client, offset in zip(clients, before):
		assert client.chat[offset:].count((COLOR, reply)) == int(
			not private or client is sender
		), (client.name, client.chat[offset:])
		assert client.settings == settings, client.settings


def qualify(args, config, root, locked):
	server = start((build_executable(args.build, 'openmotorsv')).resolve(), config)
	clients = []

	try:
		try:
			_, port = wait_banner(server)
		except AssertionError:
			if server.poll() is not None:
				raise AssertionError(('server startup', server.communicate()))

			raise

		for name in ('First', 'Second', 'Waiting'):
			clients.append(Client((build_executable(args.build, 'mh-dedicated-race-check')).resolve(),
				port, name, config, root))

			# The scripted Elimination leader is slot one. Do not let the
			# waiting client win a concurrent admission race for that slot.
			wait(server, clients, lambda: clients[-1].state is not None)
			assert clients[-1].state[0] == len(clients), clients[-1].state

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 14)
			and c.settings == (2, 0, 'Goldbridge') for c in clients))
		first, second, waiting = clients

		if locked:
			for command, reply in (('/trackup', 'Track locked!'),
				('/trackdown', 'Track locked!'),
				('/track Redrock', 'Track locked!'),
				('/track', 'Track locked!'),
				('/mode', 'Gamemode locked!'),
				('/gamemode', 'Gamemode locked!'),
				('/grid', 'Gridmode locked!')):
				change(server, clients, first, command, (2, 0, 'Goldbridge'), reply, True)

			print('Settings: all track/mode/grid locks preserve rosters and reply privately', flush=True)
		else:
			print('Settings: three clients joined; checking commands', flush=True)
			first.send('ready')
			ready = 1 << first.state[0]
			wait(server, clients, lambda: all(c.state[3] == ready for c in clients))

			for command, track in (('/trackdown', 'Redrock'), ('/trackup', 'Goldbridge'),
				('/trackup', 'Redrock'), ('/trackup', 'Goldbridge'),
				('/track rEd', 'Redrock')):
				change(server, clients, first, command, (2, 0, track), f'First sets track to: {track}')
				assert all(c.state[3] == ready for c in clients)

			change(server, clients, first, '/track Missing', (2, 0, 'Redrock'), "Can't find track: Missing", True)
			change(server, clients, first, '/track', (2, 0, 'Redrock'), 'Unknown command: /track', True)
			change(server, clients, first, '/mode', (2, 1, 'Redrock'), 'First sets gamemode to: Elimination')
			change(server, clients, first, '/gamemode ignored', (2, 0, 'Redrock'), 'First sets gamemode to: Normal')

			for grid in ('LastRace', 'ScoreNormal', 'ScoreReverse', 'TeamRandom',
				'BestLap', 'Random', 'LastRace', 'ScoreNormal', 'ScoreReverse'):
				change(server, clients, first, '/grid', (2, 0, 'Redrock'), f'First sets gridmode to: {grid}')
				assert all(c.state[3] == ready for c in clients)

			waiting.send('notready')
			wait(server, clients, lambda: all(c.state[4] == 1 << waiting.state[0] for c in clients))
			second.send('ready')
			wait(server, clients, lambda: first.state[1] == second.state[1] == RACING, 30)
			assert waiting.state[1] == LOBBY
			racers = sorted((first, second), key=lambda c: c.state[0])

			for position, client in enumerate(racers, 1):
				assert client.loads[-1][2] == 'Redrock', client.loads
				assert client.grids[-1] == (client.state[0], 1, position), client.grids

			for command in ('/trackup', '/trackdown', '/track Goldbridge', '/mode', '/gamemode', '/grid'):
				change(server, clients, waiting, command, (2, 0, 'Redrock'), 'Race in progress!', True)

			print('Settings: waiting for race results', flush=True)
			wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results
				and c.results[-1][1] == 1 and len(c.rows.get(1, [])) == 12
				and c.settings == (2, 0, 'Goldbridge') for c in clients), 100)

			for client in clients:
				participants = [row for row in client.rows[1] if row[4]]
				assert {row[4] for row in participants} == {'First', 'Second'}, client.rows
				assert all(row[2] > 0 and row[3] > 0 for row in participants), client.rows
				assert client.rows[1] == first.rows[1]

			print('Settings: selected Redrock route, ScoreReverse grid, Normal completion and rotation pass', flush=True)
			change(server, clients, first, '/mode', (2, 1, 'Goldbridge'), 'First sets gamemode to: Elimination')
			first.send('ready')
			second.send('ready')
			wait(server, clients, lambda: first.state[1] == second.state[1] == RACING, 30)
			print('Settings: waiting for race results', flush=True)
			wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results
				and c.results[-1][1] == 2 and len(c.rows.get(2, [])) == 12
				and c.settings == (2, 1, 'Redrock') for c in clients), 100)

			for client in (first, second):
				assert client.loads[-1][1:3] == (2, 'Goldbridge'), client.loads
				assert client.results[-1][3] == 2, client.results
				own = next(row for row in client.rows[2] if row[4] == client.name)
				assert client.results[-1][4] == client.results[-2][4] + (12 if own[0] == 1 else 10), client.results
				assert client.rows[2] == first.rows[2]

			assert waiting.results[-1][3:5] == (0, 0), waiting.results
			assert waiting.rows[2] == first.rows[2]
			assert any('has been eliminated by' in text for _, text in first.chat), first.chat
			print('Settings: selected Elimination mode completes the next race and retains scores', flush=True)

		for client in clients:
			finish(client)

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


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-settings-') as temporary:
		root = Path(temporary)

		for name in ('goldbridge', 'redrock'):
			synthetic_course(root / (name + '.mot'))

		config = root / 'server.cfg'

		for locked in (False, True):
			config.write_text('''SessionName Settings
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
Grid Random
Laps 2
''' + ('LockTrack\nLockMode\nLockGrid\n' if locked else ''))
			qualify(args, config, root, locked)

	print('Dedicated track/mode/grid selection and locks passed through real native UDP sessions.')


if __name__ == '__main__':
	main()
