#!/usr/bin/env python3
"""Qualify majority/operator score clearing after real native UDP races."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import tempfile
import time

from tests.checks.live.check_dedicated_races import Client, LOBBY, RACING, wait
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.live.check_dedicated_console import Console
from tests.checks.support.server import synthetic_course, start, wait_banner

COLOR = 0xe06820
MAJORITY = 'Score and gridstats cleared by majority vote...'
FORCED = 'Score and gridstats cleared...'


def vote(server, clients, sender, command, repeated=False):
	before = [len(c.chat) for c in clients]
	sender.send('chat ' + command)
	text = ('You have already voted for clearing of the score!' if repeated else
		sender.name + ' votes for clearing of the score.')
	wait(server, clients, lambda: all((COLOR, text) in c.chat[n:]
		for c, n in zip(clients, before) if not repeated or c is sender))
	until = time.monotonic() + .2
	wait(server, clients, lambda: time.monotonic() >= until)

	for client, offset in zip(clients, before):
		assert client.chat[offset:].count((COLOR, text)) == int(not repeated or client is sender)


def cleared(clients):
	return all(len(c.scores) == 12 and all(s == (0, 0, 0, 0) for s in c.scores.values())
		for c in clients)


def qualify(args, config, root):
	server = start((build_executable(args.build, 'openmotorsv')).resolve(), config)
	clients = []

	try:
		_, port = wait_banner(server)
		console = Console(server)
		binary = (build_executable(args.build, 'mh-dedicated-race-check')).resolve()

		for name in ('First', 'Second', 'Third', 'Fourth'):
			clients.append(Client(binary, port, name, config, root))

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 30) for c in clients))

		for round_number in (1, 2):
			for client in clients:
				client.send('ready')

			wait(server, clients, lambda: all(c.state[1] == RACING for c in clients), 30)
			wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results
				and c.results[-1][1] == round_number
				and len(c.rows.get(round_number, [])) == 12
				and len(c.scores) == 12 for c in clients), 100)
			slots = [c.state[0] for c in clients]
			assert all(c.scores[s][0] == 1 and c.scores[s][1] > 0
				for c in clients for s in slots), [c.scores for c in clients]
			assert all(c.rows[round_number] == clients[0].rows[round_number] for c in clients)
			saved = [dict(c.scores) for c in clients]
			clients[0].send('ready')
			ready_mask = 1 << clients[0].state[0]
			wait(server, clients, lambda: all(c.state[3] == ready_mask for c in clients))
			vote(server, clients, clients[0], '/clear')
			vote(server, clients, clients[0], '/clearscore', repeated=True)
			assert all(c.scores == scores for c, scores in zip(clients, saved))

			if round_number == 1:
				vote(server, clients, clients[1], '/clearscore ignored')
				assert all(c.scores == scores for c, scores in zip(clients, saved)), 'half the lobby cleared scores'
				vote(server, clients, clients[2], '/CLEAR')
				text = MAJORITY
			else:
				console.check(clients, '/clearscore', FORCED, clients[0].settings)
				text = FORCED

			wait(server, clients, lambda: cleared(clients) and all((COLOR, text) in c.chat for c in clients))
			assert all(c.state[3] == ready_mask and c.results[-1][1] == round_number for c in clients)
			print(f'Score clear: round {round_number}, {text}, readiness and last-race table retained', flush=True)

		# A newcomer receives cleared scores; existing clients keep the table.
		late = Client(binary, port, 'Late', config, root)
		clients.append(late)
		wait(server, clients, lambda: late.state and late.state[1] == LOBBY
			and cleared(clients))
		assert all(c.rows[2] == clients[0].rows[2] for c in clients[:-1])
		vote(server, clients, clients[0], '/clear')  # Previous vote was reset.
		assert cleared(clients)

		for client in clients:
			finish(client)

		console.send('quit')
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

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-score-clear-') as temporary:
		root = Path(temporary)
		synthetic_course(root / 'goldbridge.mot')
		config = root / 'server.cfg'
		config.write_text('''SessionName Scores
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

	print('Dedicated score clearing passed through two native UDP races and a late join.')


if __name__ == '__main__':
	main()
