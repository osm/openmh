#!/usr/bin/env python3
"""Check native dedicated kick voting, timed IP rejection and later readmission."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import tempfile
import time

from tests.checks.live.check_dedicated_bans import rejected
from tests.checks.live.check_dedicated_console import Console
from tests.checks.live.check_dedicated_information import settle
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.live.check_dedicated_races import Client, LOBBY, wait
from tests.checks.support.server import synthetic_course, start, wait_banner

COLOR = 0xe06820


def vote(server, clients, sender, target):
	before = [len(c.chat) for c in clients]
	sender.send(f'chat /kick {target.state[0]}')
	expected = (COLOR, sender.name + ' votes for kicking player: ' + target.name)
	wait(server, clients, lambda: all(expected in c.chat[n:] for c, n in zip(clients, before)))
	settle(server, clients)
	assert all(c.state[1:3] == (LOBBY, 30) for c in clients)


def qualify(args, config, root):
	binary = (build_executable(args.build, 'mh-dedicated-race-check')).resolve()
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

		for name in ('First', 'Second', 'Third', 'Fourth'):
			clients.append(Client(binary, port, name, config, root))

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 30)
			for c in clients))
		first, second, third, victim = clients
		vote(server, clients, first, third)
		vote(server, clients, first, victim)
		vote(server, clients, first, victim)
		vote(server, clients, second, victim)
		before = [len(c.chat) for c in clients]
		first.send('chat /kick 99')
		wait(server, clients, lambda: (COLOR, 'No player: 99') in first.chat[before[0]:])
		settle(server, clients)
		assert all(len(c.chat) == n for c, n in zip(clients[1:], before[1:])), 'invalid target reply leaked'
		print('Kick votes: replacement/repetition count once, two of four is insufficient, invalid target replies privately', flush=True)

		survivors = clients[:3]
		slot = victim.state[0]
		started = time.monotonic()
		third.send(f'chat /kick {slot}')
		wait(server, survivors, lambda: victim.process.poll() is not None)
		output, errors = victim.process.communicate(timeout=5)
		notice = 'Fourth was kicked and banned(1 mins) by majority vote...'
		assert victim.process.returncode == 0 and not errors, (output, errors)
		assert notice.encode() in output and b'You got kicked by the server!' in output, output
		wait(server, survivors, lambda: all(c.state[1:3] == (LOBBY, 30 & ~(1 << slot)) and
			(COLOR, notice) in c.chat for c in survivors))
		rejected(binary, port, config, root)
		before = [len(c.chat) for c in survivors]
		first.send('chat still connected')
		wait(server, survivors, lambda: all(any(text == 'First: still connected'
			for _, text in c.chat[n:])
			for c, n in zip(survivors[1:], before[1:])))
		print('Kick votes: three of four announces/removes the target, bans its IP, and leaves existing same-IP peers connected', flush=True)

		wait(server, survivors, lambda: time.monotonic() >= started + 61.5, 65)
		replacement = Client(binary, port, 'FourthAgain', config, root)
		clients.append(replacement)
		active = survivors + [replacement]
		wait(server, active, lambda: all(c.state and c.state[1:3] == (LOBBY, 30)
			for c in active))
		assert replacement.state[0] == slot

		# The preceding majority must not leave votes aimed at the reused slot.
		vote(server, active, first, replacement)
		print('Kick votes: the one-minute ban expires, the slot can be reused, and prior votes were cleared', flush=True)

		for client in active:
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

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-kick-votes-') as temporary:
		root = Path(temporary)
		synthetic_course(root / 'goldbridge.mot')
		config = root / 'server.cfg'
		config.write_text('''SessionName Votes
ServerName Console
NetworkProtocol legacy
PortNumber 0
MaxPlayers 12
MaxRate 30
MinReady 12
BanTime 1
AllReady
StartTrack Goldbridge
Track Goldbridge goldbridge.mot
''')
		qualify(args, config, root)

	print('Dedicated client kick voting and timed bans passed with real native UDP clients.')


if __name__ == '__main__':
	main()
