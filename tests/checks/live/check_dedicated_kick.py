#!/usr/bin/env python3
"""Check console kicks, client notification, roster removal and readmission."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import tempfile

from tests.checks.live.check_dedicated_console import Console
from tests.checks.live.check_dedicated_information import console_reply, settle
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.live.check_dedicated_races import Client, LOBBY, wait
from tests.checks.support.server import synthetic_course, start, wait_banner


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

		for name in ('First', 'Second', 'Third'):
			clients.append(Client((build_executable(args.build, 'mh-dedicated-race-check')).resolve(),
				port, name, config, root))

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 14)
			for c in clients))
		settle(server, clients)

		for slot in (0, 4, 13, -1):
			console_reply(console, clients, f'/kick {slot}', [f'No player with number: {slot}'])

		victim = clients[1]
		slot = victim.state[0]
		survivors = [c for c in clients if c is not victim]
		chats = [len(c.chat) for c in survivors]
		console.poll()
		before = len(console.lines)
		console.send(f'/kick {slot}')
		wait(server, survivors, lambda: victim.process.poll() is not None)
		output, errors = victim.process.communicate(timeout=10)
		assert victim.process.returncode == 0 and not errors, (output, errors)
		assert b'You got kicked by the server!' in output, output
		wait(server, survivors, lambda: all(c.state[1:3] == (LOBBY, 14 & ~(1 << slot))
			for c in survivors))
		settle(server, survivors)
		console.poll()
		assert console.lines[before:] == ['Second disconnected (kicked).',
			'Player Second kicked!'], console.lines[before:]
		assert all(len(c.chat) == n for c, n in zip(survivors, chats)), 'kick leaked into chat'
		console_reply(console, survivors, f'/kick {slot}', [f'No player with number: {slot}'])
		print('Kick: only the selected client receives the kick and exits; remaining clients get the updated roster', flush=True)

		replacement = Client((build_executable(args.build, 'mh-dedicated-race-check')).resolve(),
			port, 'SecondAgain', config, root)
		clients.append(replacement)
		active = survivors + [replacement]
		wait(server, active, lambda: all(c.state and c.state[1:3] == (LOBBY, 14)
			for c in active))
		assert replacement.state[0] == slot, replacement.state
		print('Kick: the same IP can immediately rejoin and reuse the released slot; operator kicks do not ban', flush=True)

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

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-kick-') as temporary:
		root = Path(temporary)
		synthetic_course(root / 'goldbridge.mot')
		config = root / 'server.cfg'
		config.write_text('''SessionName Kick
ServerName Console
NetworkProtocol legacy
PortNumber 0
MaxPlayers 12
MaxRate 30
MinReady 12
AllReady
BanTime 10
StartTrack Goldbridge
Track Goldbridge goldbridge.mot
GameMode Normal
Laps 1
''')
		qualify(args, config, root)

	print('Dedicated operator kicks passed through the real host and clients.')


if __name__ == '__main__':
	main()
