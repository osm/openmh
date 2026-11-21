#!/usr/bin/env python3
"""Check original dedicated information replies through stdin and native UDP."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import tempfile
import time

from tests.checks.live.check_dedicated_console import Console
from tests.checks.live.check_dedicated_races import Client, DRIVERS, LOBBY, RACING, wait
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.support.server import synthetic_course, start, wait_banner

COLOR = 0xe06820
CLIENT_HELP = [
	'/msg <PlayerName> <Message>, Send message to player',
	'/msgteam <Message>, Send message to team',
	'/msgrace <Message>, Send message to race players',
	'/kick, <playernumber>, Vote for kicking of a player',
	'/users, List players and playernumbers',
	'/grid, Toggle gridmode',
	'/clear, Vote for score clearing',
	'/mode, Toggle gamemode',
	'/lapdown, Toggle lap down',
	'/lapup, Toggle lap up',
	'/laps <laps>, Change number of laps',
	'/trackdown, Toggle track down',
	'/trackup, Toggle track up',
	'/track <trackname>, Change track',
	'/tracks, List available tracks',
	'/status, Get game status',
	'/notready, You are NOT ready to race',
	'/ready, You are ready to race',
]
OPERATOR_HELP = [
	'',
	'/quit, Quit server',
	"/kick <player number>, Kick player, get player number with '/status'",
	'/password <password name>, Set password',
	'/laps <laps>, Change number of laps',
	'/gamemode <mode>, Change gamemode',
	'/track <track>, Change track',
	'/tracks, List available tracks',
	'/name <new name>, Change your name',
	'/clear, Force score clearing',
	'/user <player number>, Get extra user info',
	'/status, Get game status and player numbers',
	'/ready, Force race start',
	'',
]
STATUS = ['MaxPlayers: 12', 'MaxRaceTime: 60 min', 'EndCountdown: 30 sec',
	'UnlockedCars: 10', 'MinReady: 1', 'MaxRate: 30',
	'GridMode: Random', 'Flags: AllReady']


def settle(server, clients):
	deadline = time.monotonic() + .3
	wait(server, clients, lambda: time.monotonic() >= deadline)


def private_reply(server, clients, sender, command, expected):
	before = [len(c.chat) for c in clients]
	sender.send('chat ' + command)
	start = before[clients.index(sender)]
	wait(server, clients, lambda: len(sender.chat) >= start + 1 + len(expected))
	settle(server, clients)

	for client, offset in zip(clients, before):
		want = ([(0x123456, sender.name + ': ' + command)] +
			[(COLOR, line) for line in expected]) if client is sender else []
		assert client.chat[offset:] == want, (command, client.name, client.chat[offset:], want)


def console_reply(console, clients, command, expected):
	console.poll()
	before = len(console.lines)
	chats = [len(c.chat) for c in clients]
	console.send(command)

	def received():
		console.poll()

		return len(console.lines) >= before + len(expected)

	wait(console.server, clients, received)
	settle(console.server, clients)
	console.poll()
	assert console.lines[before:] == expected, (command, console.lines[before:], expected)
	assert all(len(c.chat) == n for c, n in zip(clients, chats)), command


def users(clients):
	return [f'{c.state[0]}. {c.name}, Rate: 30, Control: K, Horn: Honker1'
		for c in sorted(clients, key=lambda c: c.state[0])]


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

		for i in range(12):
			clients.append(Client((build_executable(args.build, 'mh-dedicated-race-check')).resolve(),
				port, f'Driver{i + 1:02}', config, root))

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, DRIVERS)
			for c in clients))
		settle(server, clients)
		console_reply(console, clients, '/help', OPERATOR_HELP)
		console_reply(console, clients, '/commands', OPERATOR_HELP)
		console_reply(console, clients, '/status', STATUS + [
			f'{c.state[0]}. {c.name} is NOT ready.'
			for c in sorted(clients, key=lambda c: c.state[0])])
		private_reply(server, clients, clients[0], '/helpserver', CLIENT_HELP)
		private_reply(server, clients, clients[5], '/status', STATUS)
		private_reply(server, clients, clients[11], '/users', users(clients))
		print('Information: complete help/status/users text, ordering and private audiences pass with twelve clients', flush=True)

		racers, waiting = clients[:2], clients[2:]

		for client in waiting:
			client.send('notready')

		for client in racers:
			client.send('ready')

		wait(server, clients, lambda: all(c.state[1] == RACING for c in racers), 30)
		settle(server, clients)
		private_reply(server, clients, waiting[0], '/helpserver', CLIENT_HELP)
		private_reply(server, clients, waiting[0], '/status', STATUS)
		private_reply(server, clients, waiting[0], '/users', users(clients))
		console.send('/lobby')
		wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results for c in clients))
		settle(server, clients)
		private_reply(server, clients, clients[0], '/users', users(clients))
		print('Information: waiting clients can inspect both racing and lobby players; replies survive race return', flush=True)

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

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-information-') as temporary:
		root = Path(temporary)
		synthetic_course(root / 'goldbridge.mot')
		config = root / 'server.cfg'
		config.write_text('''SessionName Information
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
Laps 5
''')
		qualify(args, config, root)

	print('Dedicated information commands passed through stdin and native UDP.')


if __name__ == '__main__':
	main()
