"""Launch a server and read its bounded startup announcement."""

import subprocess
import select
import re
import os
import time
import math

from tests.checks.support.tbl import reference_encode


def start(binary, config, extra=()):
	return subprocess.Popen([str(binary), '--config', str(config), *extra],
		stdin=subprocess.PIPE, stdout=subprocess.PIPE,
		stderr=subprocess.PIPE, text=True)


def wait_banner(process):
	match, _ = wait_announcement(process, rb'Listening: 127\.0\.0\.1:(\d+)')

	return ('127.0.0.1', int(match[1]))


def synthetic_course(path):
	"""Write a closed loop with four checkpoint groups for race sessions."""

	segments = 64
	points = [(100 * math.sin(2 * math.pi * index / segments),
		100 * math.cos(2 * math.pi * index / segments))
		for index in range(segments)]
	points.append(points[0])
	motion = f'LWMO\n1\n9\n{len(points)}\n'

	for index, (x, z) in enumerate(points):
		motion += f'{x} 0 {z} 0 0 0 1 1 1\n{index * 10} 1 0 0 0\n'

	path.write_bytes(reference_encode(motion.encode()))


def synthetic_config(directory):
	synthetic_course(directory / 'fixture.mot')
	config = directory / 'server.cfg'
	config.write_text('NetworkProtocol openmh\nPortNumber 0\n'
		'SessionName Fixture\nServerName Fixture\n'
		'StartTrack Fixture\nTrack Fixture fixture.mot\n')

	return config


def legacy_config(directory, filename='server.cfg', players=12):
	"""A complete legacy lobby fixture, including its synthetic course catalog."""

	names = ['Goldbridge', 'Redrock'] + [f'Circuit{i}' for i in range(3, 17)]
	tracks = []

	for name in names:
		path = directory / (name.lower() + '.mot')
		synthetic_course(path)
		tracks.append(f'Track {name} {path.name}\n')

	config = directory / filename
	config.write_text('NetworkProtocol legacy\nPortNumber 0\n'
		f'MaxPlayers {players}\nMaxRate 30\nMinReady 2\n'
		'SessionName Fixture\nServerName Console\n'
		'LobbyTimeOut 10\nStartTrack Goldbridge\n'
		+ ''.join(tracks))

	return config


def wait_announcement(process, expression):
	startup = bytearray()
	deadline = time.monotonic() + 10

	while True:
		remaining = deadline - time.monotonic()

		if remaining <= 0 or not select.select([process.stdout], [], [], remaining)[0]:
			raise RuntimeError('Server did not announce its listening address')

		chunk = os.read(process.stdout.fileno(), 4096)

		if not chunk:
			raise RuntimeError('Server exited before its startup announcement')

		startup.extend(chunk)

		if len(startup) > 8192:
			raise RuntimeError('Oversized startup announcement')

		match = re.search(expression, startup)

		if match:
			return match, startup


def wait_browser_banner(process, address='127.0.0.1'):
	expression = (rb'Browser connections: UDP ' + re.escape(address.encode())
		+ rb':(\d+); signaling HTTP 127\.0\.0\.1:(\d+)/')
	match, startup = wait_announcement(process, expression)
	native = re.search(rb'Listening: ' + re.escape(address.encode()) + rb':(\d+)', startup)

	if native is None:
		raise RuntimeError('Missing native UDP announcement')

	udp, http = (int(value) for value in match.groups())

	return {'native_port': int(native[1]), 'udp_port': udp, 'http_port': http}, startup
