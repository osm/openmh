#!/usr/bin/env python3
"""Qualify dedicated console identity and mutable admission passwords over UDP."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import subprocess
import tempfile

from tests.checks.live.check_dedicated_console import Console
from tests.checks.live.check_dedicated_information import console_reply, settle
from tests.checks.live.check_dedicated_races import Client, LOBBY, wait
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.live.check_dedicated_messages import NOTICE_COLOR
from tests.checks.support.server import synthetic_course, start, wait_banner


def rejected(binary, port, config, root, password):
	process = subprocess.Popen([str(binary), str(port), 'Rejected', str(config),
		str(root), '', password], stdin=subprocess.PIPE,
		stdout=subprocess.PIPE, stderr=subprocess.PIPE)

	try:
		process.wait(timeout=10)
		output, errors = process.communicate()
		assert process.returncode == 1 and errors == b'Invalid password!\n', (output, errors)
	finally:
		if process.poll() is None:
			process.terminate()

		process.communicate(timeout=5)


def qualify(args, config, root, legacy):
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
		rejected(binary, port, config, root, '')

		for name in ('First', 'Second'):
			clients.append(Client(binary, port, name, config, root, password='sTARTUP' if legacy else 'Startup'))

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 6)
			and c.server[2] == 'Console' for c in clients))
		settle(server, clients)
		console_reply(console, clients, '/name', ['Current player name: Console'])

		for command, name in (('/name RaceMaster', 'RaceMaster'),
			('/nick abcdefghijklmnopq', 'abcdefghijklmno'),
			('/name Two Words', 'Two'), ('/nick', '')):
			console_reply(console, clients, command, ['Name changed to: ' + name])
			wait(server, clients, lambda: all(c.server[2] == name for c in clients))
			assert all(c.server[0] & 3 == c.server[1] & 3 == 0 for c in clients)

		console_reply(console, clients, '/name Marshal', ['Name changed to: Marshal'])
		wait(server, clients, lambda: all(c.server[2] == 'Marshal' for c in clients))
		before = [len(c.chat) for c in clients]
		console.send('Welcome racers')
		wait(server, clients, lambda: all((NOTICE_COLOR, 'console: Welcome racers') in c.chat[n:]
			for c, n in zip(clients, before)))
		settle(server, clients)
		print('Identity: queries, aliases, truncation, empty name and profile publication preserve the hidden console', flush=True)

		console_reply(console, clients, '/password NewSecret', ['Password changed.'])
		rejected(binary, port, config, root, 'Startup')
		rejected(binary, port, config, root, '')

		if not legacy:
			rejected(binary, port, config, root, 'nEWSECRET')

		clients.append(Client(binary, port, 'Third', config, root, password='nEWSECRET' if legacy else 'NewSecret'))
		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 14)
			and c.server[2] == 'Marshal' for c in clients))
		settle(server, clients)
		console_reply(console, clients, '/password', ['Password removed'])
		clients.append(Client(binary, port, 'Fourth', config, root))
		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 30)
			and c.server[2] == 'Marshal' for c in clients))
		assert 'Password Startup' in config.read_text(), 'operator mutated the configuration file'
		print('Password changes govern new admissions', flush=True)

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

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-identity-') as temporary:
		root = Path(temporary)
		synthetic_course(root / 'goldbridge.mot')
		config = root / 'server.cfg'
		settings = '''SessionName Identity
ServerName Console
Password Startup
NetworkProtocol {protocol}
PortNumber 0
MaxPlayers 12
MaxRate 30
MinReady 1
AllReady
StartTrack Goldbridge
Track Goldbridge goldbridge.mot
GameMode Normal
Laps 5
'''

		for protocol in ('legacy', 'openmh'):
			config.write_text(settings.format(protocol=protocol))
			qualify(args, config, root, protocol == 'legacy')

	print('Dedicated console identity and password qualification passed.')


if __name__ == '__main__':
	main()
