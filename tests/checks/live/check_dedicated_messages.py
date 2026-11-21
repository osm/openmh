#!/usr/bin/env python3
"""Check dedicated private/team/race chat through live UDP client sessions."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import tempfile
import time

from tests.checks.live.check_dedicated_races import Client, LOBBY, RACING, wait
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.support.server import synthetic_course, start, wait_banner

PROFILE_COLOR = 0x2468ac
NOTICE_COLOR = 0xe06820
CHAT_COLOR = 0x123456


def message(server, clients, sender, command, text, recipients, color):
	before = {c.name: len(c.chat) for c in clients}
	sender.send('chat ' + command)
	expected = (color, text)
	wait(server, clients, lambda: all(expected in c.chat[before[c.name]:]
		for c in clients if c.name in recipients))

	# Continue draining every receiver so an incorrect extra audience fails.
	deadline = time.monotonic() + .3
	wait(server, clients, lambda: time.monotonic() >= deadline)

	for client in clients:
		actual = client.chat[before[client.name]:]
		assert actual.count(expected) == int(client.name in recipients), (
			client.name, command, actual
		)
		assert not any('Unknown command:' in line for _, line in actual), actual


def qualify(args, config, root):
	server = start((build_executable(args.build, 'openmotorsv')).resolve(), config)
	clients = []

	try:
		_, port = wait_banner(server)

		for name, team in (('Alpha', 'Red'), ('Beta', 'RED'), ('Gamma', 'Blue'),
			('Waiting', 'red'), ('Other', 'Blue')):
			clients.append(Client((build_executable(args.build, 'mh-dedicated-race-check')).resolve(),
				port, name, config, root, team))

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 62) for c in clients))
		alpha, beta, gamma, waiting, other = clients
		message(server, clients, alpha, '/msg bEtA private lobby', '*Alpha* private lobby',
			{'Alpha', 'Beta'}, PROFILE_COLOR)
		message(server, clients, alpha, '/msg Alpha self', 'No player: Alpha', {'Alpha'}, NOTICE_COLOR)
		message(server, clients, alpha, '/msg Missing absent', 'No player: Missing', {'Alpha'}, NOTICE_COLOR)
		message(server, clients, alpha, '/msgteam lobby team', '(Alpha): lobby team',
			{'Alpha', 'Beta', 'Waiting'}, PROFILE_COLOR)
		message(server, clients, alpha, '/msgrace empty race', 'Lobby-> Alpha: empty race',
			{'Alpha'}, PROFILE_COLOR)
		print('Messages: lobby private/self/missing, team case folding and race audience pass', flush=True)

		waiting.send('notready')
		other.send('notready')
		passing = sum(1 << c.state[0] for c in (waiting, other))
		wait(server, clients, lambda: all(c.state[4] == passing for c in clients))

		for client in (alpha, beta, gamma):
			client.send('ready')

		wait(server, clients, lambda: all(c.state[1] == RACING for c in (alpha, beta, gamma)), 30)
		assert waiting.state[1] == other.state[1] == LOBBY
		message(server, clients, waiting, '/msg Alpha private race', '*Waiting* private race',
			{'Waiting', 'Alpha'}, PROFILE_COLOR)
		message(server, clients, waiting, '/msgteam mixed team', '(Waiting): mixed team',
			{'Alpha', 'Beta', 'Waiting'}, PROFILE_COLOR)
		message(server, clients, waiting, '/msgrace hurry', 'Lobby-> Waiting: hurry',
			{'Alpha', 'Beta', 'Gamma', 'Waiting'}, PROFILE_COLOR)
		message(server, clients, waiting, 'ordinary lobby', 'Waiting: ordinary lobby',
			{'Waiting', 'Other'}, CHAT_COLOR)
		message(server, clients, alpha, 'ordinary race', 'Alpha: ordinary race',
			{'Alpha', 'Beta', 'Gamma'}, CHAT_COLOR)

		# The original race receiver forwards racing text without interpreting
		# lobby commands. The native local client must retain its own echo too.
		message(server, clients, alpha, '/msgteam raw racing text', 'Alpha: /msgteam raw racing text',
			{'Alpha', 'Beta', 'Gamma'}, CHAT_COLOR)
		print('Messages: mixed lobby/race private and team delivery, race-only messages and ordinary audiences pass', flush=True)

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

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-messages-') as temporary:
		root = Path(temporary)
		synthetic_course(root / 'goldbridge.mot')
		config = root / 'server.cfg'
		config.write_text('''SessionName Messages
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

	print('Dedicated directed messages and lobby/race audiences passed over native UDP.')


if __name__ == '__main__':
	main()
