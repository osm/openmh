#!/usr/bin/env python3
"""Check dedicated client lap commands, locks, publication and race permission."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import tempfile
import time

from tests.checks.live.check_dedicated_races import Client, LOBBY, RACING, wait
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.support.server import synthetic_course, start, wait_banner

COLOR = 0xe06820


def change(server, clients, sender, command, expected, reply, private=False):
	before = [len(c.chat) for c in clients]
	sender.send('chat ' + command)
	wait(server, clients, lambda: all(c.settings and c.settings[0] == expected
		and ((COLOR, reply) in c.chat[n:] if not private or c is sender else True)
		for c, n in zip(clients, before)))
	deadline = time.monotonic() + .2
	wait(server, clients, lambda: time.monotonic() >= deadline)

	for client, offset in zip(clients, before):
		assert client.chat[offset:].count((COLOR, reply)) == int(
			not private or client is sender
		), (client.name, client.chat[offset:])
		assert client.settings == (expected, 0, 'Goldbridge'), client.settings


def qualify(args, config, root, locked):
	server = start((build_executable(args.build, 'openmotorsv')).resolve(), config)
	clients = []

	try:
		_, port = wait_banner(server)

		for name in ('First', 'Second', 'Waiting'):
			clients.append(Client((build_executable(args.build, 'mh-dedicated-race-check')).resolve(),
				port, name, config, root))

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 14)
			and c.settings == (5, 0, 'Goldbridge') for c in clients))
		first, second, waiting = clients

		if locked:
			for command in ('/lapup', '/lapdown', '/laps 2', '/laps'):
				change(server, clients, first, command, 5, 'Laps locked!', private=True)

			print('Laps: locked commands leave all rosters unchanged and reply only to the author', flush=True)
		else:
			first.send('ready')
			ready = 1 << first.state[0]
			wait(server, clients, lambda: all(c.state[3] == ready for c in clients))

			for command, laps in (('/lapup', 10), ('/lapdown', 5), ('/laps 4', 4),
				('/lapup', 5), ('/lapdown', 3), ('/laps 0', 1),
				('/lapdown', 1), ('/laps 26', 25), ('/lapup', 25),
				('/lapdown', 20), ('/laps 2suffix', 2)):
				change(server, clients, first, command, laps, f'First sets laps to: {laps}')
				assert all(c.state[3] == ready for c in clients), 'settings erased readiness'

			change(server, clients, first, '/laps', 2, 'Unknown command: /laps', private=True)
			waiting.send('notready')
			wait(server, clients, lambda: all(c.state[4] == 1 << waiting.state[0] for c in clients))
			second.send('ready')
			wait(server, clients, lambda: first.state[1] == second.state[1] == RACING, 30)
			assert waiting.state[1] == LOBBY

			for command in ('/laps 9', '/lapup', '/lapdown', '/ready', '/notready'):
				change(server, clients, waiting, command, 2, 'Race in progress!', private=True)

			print('Laps: stepping, clamp, reliable roster refresh, retained readiness and race-time permissions pass', flush=True)

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

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-laps-') as temporary:
		root = Path(temporary)
		synthetic_course(root / 'goldbridge.mot')
		config = root / 'server.cfg'

		for locked in (False, True):
			config.write_text('''SessionName Laps
NetworkProtocol legacy
PortNumber 0
MaxPlayers 12
MaxRate 30
MinReady 1
AllReady
StartTrack Goldbridge
Track Goldbridge goldbridge.mot
GameMode Normal
Laps 5
''' + ('LockLaps\n' if locked else ''))
			qualify(args, config, root, locked)

	print('Dedicated lap commands and LockLaps passed through real native UDP sessions.')


if __name__ == '__main__':
	main()
