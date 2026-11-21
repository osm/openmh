#!/usr/bin/env python3
"""Qualify passing, late arrival, race departure and replacement over real UDP."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import tempfile

from tests.checks.live.check_dedicated_races import Client, DRIVERS, LOBBY, POINTS, RACING, wait
from tests.checks.support.server import synthetic_course, start, wait_banner


def finish(client):
	client.send('quit')
	output, errors = client.process.communicate(timeout=5)
	assert client.process.returncode == 0 and not errors, (client.name, output, errors)


def results_ready(clients, track, races):
	return all(c.state[1] == LOBBY and c.results and c.results[-1][5] == track
		and c.results[-1][3] == races[c.name]
		and len(c.rows.get(c.results[-1][1], [])) == 12 for c in clients)


def check_results(clients, participants, previous):
	common = None

	for client in clients:
		rows = client.rows[client.results[-1][1]]
		actual = [r for r in rows if r[4]]
		assert {r[4] for r in actual} == participants, rows
		assert sorted(r[0] for r in actual) == list(range(1, len(participants) + 1)), rows
		assert all(r[2] > 0 and r[3] > 0 for r in actual), rows
		own = next((r for r in actual if r[4] == client.name), None)
		points = previous[client.name] + (POINTS[own[0] - 1] if own else 0)
		assert client.results[-1][4] == points, (client.name, client.results, points)

		if common is None:
			common = rows

		assert rows == common, 'authoritative results differ between waiting and racing clients'


def qualify(args, config, root):
	server = start((build_executable(args.build, 'openmotorsv')).resolve(), config)
	binary = (build_executable(args.build, 'mh-dedicated-race-check')).resolve()
	clients, owned = [], []

	try:
		_, port = wait_banner(server)

		def join(name):
			client = Client(binary, port, name, config, root)
			owned.append(client)
			clients.append(client)

			return client

		for i in range(11):
			join(f'Driver{i + 1:02}')

		initial = DRIVERS & ~(1 << 12)
		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, initial)
			for c in clients))
		by_slot = {c.state[0]: c for c in clients}
		passing, departed, returned = (by_slot[i] for i in (11, 10, 9))
		passing.send('notready')
		wait(server, clients, lambda: all(c.state[4] == 1 << 11 for c in clients))
		racers = [c for c in clients if c is not passing]

		for client in racers:
			client.send('ready')

		wait(server, clients, lambda: all(c.state[1] == RACING for c in racers), 30)
		assert passing.state[1] == LOBBY and not passing.loads
		print('Membership: ten racers started; passing driver remains in lobby', flush=True)

		late = join('LateArrival')
		wait(server, clients, lambda: late.state and late.state[1:3] == (LOBBY, DRIVERS))
		assert late.state[0] == 12 and not late.loads
		clients.remove(departed)
		racers.remove(departed)
		finish(departed)
		wait(server, clients, lambda: all(not c.state[2] & (1 << 10) for c in clients))
		replacement = join('Replacement')
		wait(server, clients, lambda: replacement.state and replacement.state[1:3] == (LOBBY, DRIVERS))
		assert replacement.state[0] == 10 and not replacement.loads

		returned.send('return')
		racers.remove(returned)
		final_mask = sum(1 << c.state[0] for c in racers)
		wait(server, clients, lambda: returned.state[1] == LOBBY
			and all(c.state[5] == final_mask for c in racers))
		assert all(c.state[1] == RACING for c in racers)
		print('Membership: late join, racing disconnect/slot reuse and early lobby return pass', flush=True)

		participants = {c.name for c in racers}
		race_counts = {c.name: int(c.name in participants) for c in clients}
		wait(server, clients, lambda: results_ready(clients, 'Redrock', race_counts), 75)
		check_results(clients, participants, {c.name: 0 for c in clients})
		assert all(not c.loads for c in (passing, late, replacement))
		assert len(returned.loads) == 1
		print('Membership: only remaining racers receive first-race scores; all clients agree on results', flush=True)

		# The original passing flag survives return. Ready first makes this
		# player uncommitted; only a second ready commits to the next race.
		passing.send('ready')
		wait(server, clients, lambda: all(c.state[4] == 0 for c in clients))
		previous = {c.name: c.results[-1][4] for c in clients}
		loads = {c.name: len(c.loads) for c in clients}

		for client in clients:
			client.send('ready')

		wait(server, clients, lambda: all(c.state[1] == RACING for c in clients), 30)
		assert all(len(c.loads) == loads[c.name] + 1 and c.loads[-1][2] == 'Redrock'
			for c in clients)
		race_counts = {name: count + 1 for name, count in race_counts.items()}
		wait(server, clients, lambda: results_ready(clients, 'Goldbridge', race_counts), 75)
		check_results(clients, {c.name for c in clients}, previous)
		assert all(c.results[-1][2] == DRIVERS & ~(1 << c.state[0]) for c in clients)
		print('Membership: all twelve complete the next race with correct scores, poses and rotation', flush=True)

		for client in clients:
			finish(client)

		server.stdin.write('quit\n')
		server.stdin.flush()
		output, errors = server.communicate(timeout=5)
		assert server.returncode == 0 and not errors, (output, errors)
	finally:
		for client in owned:
			client.stop()

		if server.poll() is None:
			server.terminate()

		server.communicate(timeout=5)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-membership-') as temporary:
		root = Path(temporary)

		for name in ('goldbridge.mot', 'redrock.mot'):
			synthetic_course(root / name)

		config = root / 'server.cfg'
		config.write_text('''SessionName MembershipCheck
NetworkProtocol legacy
PortNumber 0
MaxPlayers 12
MaxRate 30
MinReady 1
AllReady
StartTrack Goldbridge
Track Goldbridge goldbridge.mot
Track Redrock redrock.mot
GameMode Normal
Grid ScoreNormal
Laps 1
EndCountdown 10
MaxRaceTime 3
StepTrack
''')
		qualify(args, config, root)

	print('Dedicated live membership qualification passed. Scripted movement; no original peer or rendered driving.')


if __name__ == '__main__':
	main()
