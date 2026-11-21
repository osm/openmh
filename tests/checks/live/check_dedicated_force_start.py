#!/usr/bin/env python3
"""Qualify operator countdowns, selected drivers and an empty forced race."""

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
READY = 'Race will start in 20 seconds...'
WAITING = 'Race will start in 20 seconds,    GO READY!!'


def force(server, console, clients, ready):
	before = [len(c.chat) for c in clients]
	started = time.monotonic()
	console.check(clients, '/ready', READY, clients[0].settings)
	wait(server, clients, lambda: all((COLOR, READY if c in ready else WAITING) in c.chat[n:]
		for c, n in zip(clients, before)))

	return started


def qualify(args, config, root):
	server = start((build_executable(args.build, 'openmotorsv')).resolve(), config)
	clients = []

	try:
		_, port = wait_banner(server)
		console = Console(server)

		for name in ('Racer', 'Passing', 'Waiting'):
			clients.append(Client((build_executable(args.build, 'mh-dedicated-race-check')).resolve(),
				port, name, config, root))

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 14) for c in clients))
		racer, passing, waiting = clients
		racer.send('ready')
		passing.send('notready')
		ready_mask, pass_mask = 1 << racer.state[0], 1 << passing.state[0]
		wait(server, clients, lambda: all(c.state[3:5] == (ready_mask, pass_mask) for c in clients))
		started = force(server, console, clients, [racer])
		wait(server, clients, lambda: time.monotonic() >= started + 2)
		assert all(c.state[1] == LOBBY and c.state[3:5] == (ready_mask, pass_mask) for c in clients)
		restarted = force(server, console, clients, [racer])
		wait(server, clients, lambda: racer.state[1] == RACING, 30)
		assert time.monotonic() - restarted >= 19.5, 'second /ready did not restart countdown'
		assert passing.state[1] == waiting.state[1] == LOBBY
		assert not passing.loads and not waiting.loads

		for client in clients:
			for notice in ('10 seconds to race start!', '5 seconds to race start!', '4', '3', '2', '1'):
				assert (COLOR, notice) in client.chat, (client.name, notice, client.chat)

		print('Force start: repeated countdown bypasses MinReady/AllReady and selects only ready drivers', flush=True)
		wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results
			and c.results[-1][1] == 1 and len(c.rows.get(1, [])) == 12
			and c.settings == (1, 0, 'Redrock') for c in clients), 100)
		assert racer.results[-1][3:5] == (1, 12), racer.results
		assert all(c.results[-1][3:5] == (0, 0) for c in clients[1:])
		assert all(c.rows[1] == racer.rows[1] for c in clients)
		scores = [c.results[-1][3:5] for c in clients]
		loads = [list(c.loads) for c in clients]
		assert all(c.state[3] == 0 for c in clients)
		force(server, console, clients, [])
		wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results[-1][1] == 2
			and len(c.rows.get(2, [])) == 12 and c.settings == (1, 0, 'Goldbridge')
			for c in clients), 30)
		assert all(c.results[-1][3:5] == score and c.loads == load
			for c, score, load in zip(clients, scores, loads))
		assert all(not row[4] for c in clients for row in c.rows[2]), 'empty race invented a participant'
		print('Force start: no ready drivers returns to lobby, rotates track and awards no scores', flush=True)

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

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-force-start-') as temporary:
		root = Path(temporary)

		for name in ('goldbridge', 'redrock'):
			synthetic_course(root / (name + '.mot'))

		config = root / 'server.cfg'
		config.write_text('''SessionName ForceStart
NetworkProtocol legacy
PortNumber 0
MaxPlayers 12
MaxRate 30
MinReady 12
AllReady
StartTrack Goldbridge
Track Goldbridge goldbridge.mot
Track Redrock redrock.mot
StepTrack
GameMode Normal
Laps 1
''')
		qualify(args, config, root)

	print('Dedicated operator force-start passed through stdin and native UDP.')


if __name__ == '__main__':
	main()
