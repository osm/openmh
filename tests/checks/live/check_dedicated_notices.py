#!/usr/bin/env python3
"""Check dedicated welcome audiences and ready/pass/missed-start notices."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import tempfile

from tests.checks.live.check_dedicated_console import Console
from tests.checks.live.check_dedicated_information import settle
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.live.check_dedicated_races import Client, LOBBY, RACING, wait
from tests.checks.support.server import synthetic_course, start, wait_banner

COLOR = 0xe06820
WELCOME = ['MinReady: 12, UnlockedCars: 14',
	'Flags: AllReady, GhostRev, LockMode',
	"Type '/helpserver' for server commands and '/ready' to race!"]


def joined(server, clients, binary, port, name, config, root):
	before = [len(c.chat) for c in clients]
	client = Client(binary, port, name, config, root)
	clients.append(client)
	wait(server, clients, lambda: (COLOR, WELCOME[-1]) in client.chat)
	settle(server, clients)
	entered = (COLOR, name + ' entered the lobby.')
	welcome = client.chat[-5:]
	assert welcome[0] == entered, welcome
	assert welcome[1][0] == COLOR and welcome[1][1].startswith('OpenMH Server '), welcome
	assert welcome[1][1][len('OpenMH Server '):], welcome
	assert welcome[2:] == [(COLOR, text) for text in WELCOME], welcome

	for other, offset in zip(clients[:-1], before):
		expected = [entered] if other.state[1] == LOBBY else []
		assert other.chat[offset:] == expected, (other.name, other.chat[offset:], expected)

	return client


def qualify(args, config, root):
	server = start((build_executable(args.build, 'openmotorsv')).resolve(), config)
	clients = []

	try:
		_, port = wait_banner(server)
		binary = (build_executable(args.build, 'mh-dedicated-race-check')).resolve()
		console = Console(server)

		for name in ('Ready', 'Passing', 'Uncommitted'):
			joined(server, clients, binary, port, name, config, root)

		ready, passing, idle = clients
		ready.send('ready')
		passing.send('notready')
		wait(server, clients, lambda: all(c.state[3:5] == (2, 4) for c in clients))
		before = [len(c.chat) for c in clients]
		console.send('/ready')
		wait(server, clients, lambda: ready.state[1] == RACING, 30)
		settle(server, clients)
		messages = ('Entering race.', 'Race started...',
			'You failed to go ready, missed race start...')

		for client, offset, text in zip(clients, before, messages):
			assert (COLOR, text) in client.chat[offset:], (client.name, client.chat[offset:])
			assert all((COLOR, other) not in client.chat[offset:]
				for other in messages if other != text)

		assert passing.state[1] == idle.state[1] == LOBBY

		# New joiners during loading/racing welcome privately; racing drivers
		# must not receive lobby-entry announcements or private server help.
		joined(server, clients, binary, port, 'Late', config, root)
		console.send('/lobby')
		wait(server, clients, lambda: all(c.state[1] == LOBBY for c in clients))

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

	print('Dedicated notices: ordered private welcomes, lobby-only entry, '
		'ready/pass/missed-start messages and late-join audiences passed')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-notices-') as temporary:
		root = Path(temporary)
		synthetic_course(root / 'course.mot')
		config = root / 'server.cfg'
		config.write_text('''SessionName Notices
NetworkProtocol legacy
PortNumber 0
MaxPlayers 12
MaxRate 30
MinReady 12
UnlockedCars 14
AllReady
GhostRev
LockMode
StartTrack Goldbridge
Track Goldbridge course.mot
Laps 1
''')
		qualify(args, config, root)


if __name__ == '__main__':
	main()
