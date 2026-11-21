#!/usr/bin/env python3
"""Qualify dedicated console event audiences through native UDP clients."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import tempfile
import time

from tests.checks.live.check_dedicated_races import Client, LOBBY, RACING, wait
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.live.check_dedicated_console import Console
from tests.checks.support.server import synthetic_course, start, wait_banner


def settle(server, clients, console, seconds=.2):
	deadline = time.monotonic() + seconds

	def done():
		console.poll()

		return time.monotonic() >= deadline

	wait(server, clients, done, seconds + 5)


def expect(server, clients, console, text, count=1, timeout=10):
	def ready():
		console.poll()

		return console.lines.count(text) >= count

	wait(server, clients, ready, timeout)
	assert console.lines.count(text) == count, (text, console.lines)


def qualify(args, config, root, mode):
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
		binary = (build_executable(args.build, 'mh-dedicated-race-check')).resolve()

		for name in ('First', 'Second', 'Waiting'):
			clients.append(Client(binary, port, name, config, root, team='TEST'))

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 14) for c in clients))

		for client in clients:
			expect(server, clients, console, client.name + ' entered the lobby.')

		first, second, waiting = clients
		first.send('chat public lobby')
		expect(server, clients, console, 'First: public lobby')

		# Private replies/group messages deliberately do not enter the console.
		before = len(console.lines)

		for command in ('/msg Second private sentinel', '/msgteam team sentinel',
			'/msgrace race sentinel', '/helpserver', '/status', '/users',
			'/missing', '/kick 99'):
			first.send('chat ' + command)

		settle(server, clients, console)
		assert len(console.lines) == before, console.lines[before:]
		assert any('private sentinel' in text for _, text in second.chat)
		assert any('team sentinel' in text for _, text in waiting.chat)
		first.send('chat /laps 1')
		expect(server, clients, console, 'First sets laps to: 1')
		first.send('chat /grid')
		expect(server, clients, console, 'First sets gridmode to: LastRace')
		first.send('chat /clear')
		expect(server, clients, console, 'First votes for clearing of the score.')
		second.send('chat /clear')
		expect(server, clients, console, 'Score and gridstats cleared by majority vote...')
		console.send('/clear')
		expect(server, clients, console, 'Score and gridstats cleared...')
		first.send(f'chat /kick {waiting.state[0]}')
		expect(server, clients, console, 'First votes for kicking player: Waiting')

		# Operator countdown has one console line, regardless of recipients.
		console.send('/ready')
		expect(server, clients, console, 'Race will start in 20 seconds...')
		first.send('ready')
		expect(server, clients, console, 'First is ready.')
		first.send('notready')
		expect(server, clients, console, 'First is not ready.')
		expect(server, clients, console, 'Race start aborted!')
		first.send('ready')
		expect(server, clients, console, 'First is ready.', count=2)

		# Automatic countdown with a waiting player; then commit everyone.
		expect(server, clients, console, 'Race will start in 20 seconds...', count=2)
		second.send('ready')
		waiting.send('notready')
		wait(server, clients, lambda: first.state[1] == second.state[1] == RACING, 30)
		expect(server, clients, console, 'Entering race.')
		expect(server, clients, console, 'All players in race. Race started!')
		assert waiting.state[1] == LOBBY
		first.send('chat race public')
		expect(server, clients, console, 'First: race public')
		before = len(console.lines)
		waiting.send('chat /laps 5')
		waiting.send('chat /msg First race private sentinel')
		settle(server, clients, console)
		assert len(console.lines) == before, console.lines[before:]

		if mode == 'Normal':
			# EOF stops operator input but must not silence later events.
			server.stdin.close()
			server.stdin = None

		wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results
			and len(c.rows.get(1, [])) == 12 for c in clients), 100)
		expect(server, clients, console, 'Back to lobby.')

		if mode == 'Elimination':
			assert sum(' has been eliminated by ' in line for line in console.lines) == 1
			assert sum(line.endswith(' is the winner!') for line in console.lines) == 1

		assert not any(line.startswith(('Time left:', 'You have been', 'You are the'))
			for line in console.lines), console.lines

		# Lost process produces a timeout, not a voluntary departure line.
		lost = Client(binary, port, 'Lost', config, root)
		clients.append(lost)
		wait(server, clients, lambda: lost.state and lost.state[1] == LOBBY)
		expect(server, clients, console, 'Lost entered the lobby.')
		lost.process.terminate()
		lost.process.communicate(timeout=5)
		clients.remove(lost)
		expect(server, clients, console, 'Lost timed out!', timeout=8)
		assert 'Lost disconnected (left the server).' not in console.lines

		for client in clients:
			finish(client)

		settle(server, [], console)

		for client in clients:
			expect(server, [], console, client.name + ' disconnected (left the server).')

		if mode == 'Normal':
			server.terminate()
		else:
			console.send('quit')

		output, errors = server.communicate(timeout=5)
		assert server.returncode == 0 and not errors, (output, errors)
		print(f'{mode}: public/private audiences, votes, countdown, race transitions, '
			'timeouts and departure events passed', flush=True)
	finally:
		for client in clients:
			client.stop()

		if server.poll() is None:
			server.terminate()

		server.communicate(timeout=5)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--mode', choices=('Normal', 'Elimination', 'both'), default='both')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-events-') as temporary:
		root = Path(temporary)
		synthetic_course(root / 'goldbridge.mot')

		for mode in ('Normal', 'Elimination') if args.mode == 'both' else (args.mode,):
			config = root / 'server.cfg'
			config.write_text(f'''SessionName Events
NetworkProtocol legacy
PortNumber 0
MaxPlayers 12
MaxRate 30
MinReady 1
StartTrack Goldbridge
Track Goldbridge goldbridge.mot
GameMode {mode}
Laps 1
EndCountdown 5
MaxRaceTime 2
LobbyTimeOut 3
''')
			qualify(args, config, root, mode)

	print('Dedicated operational console events passed through native client sessions.')


if __name__ == '__main__':
	main()
