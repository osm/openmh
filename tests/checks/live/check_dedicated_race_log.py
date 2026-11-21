#!/usr/bin/env python3
"""Check race files against native UDP results, rotation, abort and DisableIO."""

from tests.checks.support.build import build_executable
import argparse
import datetime
from pathlib import Path
import re
import socket
import tempfile

from tests.checks.live.check_dedicated_races import Client, LOBBY, RACING, wait
from tests.checks.live.check_dedicated_membership import finish
from tests.checks.live.check_dedicated_console import Console
from tests.checks.support.server import synthetic_course, start, wait_banner


def time_text(milliseconds):
	value = min(milliseconds, 59999999)

	return f'{value // 60000:03}:{value // 1000 % 60:02}.{value // 10 % 100:02}'


def check_line(line, track, mode, rows, before, after):
	fields = line.decode('latin1').split('\t')
	assert len(fields) == 102 and fields[-1] == '', fields
	stamp = datetime.datetime.strptime(fields[0] + fields[1], '%Y%m%d%H:%M')
	assert before.replace(second=0, microsecond=0) <= stamp <= after, stamp
	assert fields[2:5] == [track, '1', mode], fields[:5]
	names = {row[4] for row in rows if row[4]}

	for rank in range(1, 13):
		record = fields[5 + (rank - 1) * 8:5 + rank * 8]
		assert record[0] == str(rank)
		row = next((r for r in rows if r[0] == rank and r[4]), None)

		if row is None:
			assert record[1:] == [''] * 7, record
		else:
			assert record[1] == row[4].ljust(15), record
			assert record[2:6] == ['LOG ', 'Kobra X3       ', 'M', 'K'], record

			# One-lap races use raw elapsed time twice, even for an unfinished
			# driver. Finished rows must match the authoritative result table.
			if row[3]:
				assert record[6:] == [time_text(row[3]), time_text(row[2])], (record, row)
			else:
				assert record[6] == record[7] and re.fullmatch(r'\d{3}:\d{2}\.\d{2}', record[6])

			names.remove(row[4])

	assert not names


def qualify(args, config, root, mode, quiet):
	log = root / 'RaceLog.txt'
	prefix = b'existing records\r\n'
	log.write_bytes(prefix)

	with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
		reservation.bind(('127.0.0.1', 0))
		port = reservation.getsockname()[1]

	config.write_text(f'''SessionName LogCheck
NetworkProtocol legacy
PortNumber {port if quiet else 0}
MaxPlayers 12
MaxRate 30
MinReady 1
AllReady
StartTrack Goldbridge
Track Goldbridge goldbridge.mot
Track Redrock redrock.mot
GameMode {mode}
Laps 1
EndCountdown 5
MaxRaceTime 2
StepTrack
RaceLog {log}
''')
	server = start((build_executable(args.build, 'openmotorsv')).resolve(), config,
		('--no-console',) if quiet else ())
	clients = []

	try:
		if not quiet:
			_, port = wait_banner(server)

		console = Console(server)

		for name in ('First', 'Second', 'Third'):
			clients.append(Client((build_executable(args.build, 'mh-dedicated-race-check')).resolve(),
				port, name, config, root, team='LOG'))

		wait(server, clients, lambda: all(c.state and c.state[1:3] == (LOBBY, 14) for c in clients))
		revision = 0

		if not quiet:
			for client in clients:
				client.send('ready')

			wait(server, clients, lambda: all(c.state[1] == RACING for c in clients), 30)
			console.send('/lobby')
			wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results
				and c.results[-1][1] == 1 for c in clients))
			assert log.read_bytes() == prefix, 'aborted race was logged'
			console.send('/track Goldbridge')
			wait(server, clients, lambda: all(c.settings[2] == 'Goldbridge' for c in clients))
			revision = 1

		saved = prefix

		for round_number in (1, 2):
			before = datetime.datetime.now()

			for client in clients:
				client.send('ready')

			wait(server, clients, lambda: all(c.state[1] == RACING for c in clients), 30)
			wait(server, clients, lambda: all(c.state[1] == LOBBY and c.results
				and c.results[-1][1] == revision + round_number
				and len(c.rows.get(revision + round_number, [])) == 12 for c in clients), 100)
			data = log.read_bytes()
			assert data.startswith(saved) and data.endswith(b'\r\n'), data
			added = data[len(saved):]
			assert added.count(b'\n') == 1 and added.count(b'\r') == 1, added
			check_line(added[:-2], 'Goldbridge' if round_number == 1 else 'Redrock',
				mode, clients[0].rows[revision + round_number], before,
				datetime.datetime.now())
			saved = data
			print(f'{mode}: race {round_number} appended matching results before rotation '
				f'(DisableIO={quiet})', flush=True)

		for client in clients:
			finish(client)

		if quiet:
			server.terminate()
		else:
			console.send('quit')

		output, errors = server.communicate(timeout=5)
		assert server.returncode == 0 and not errors, (output, errors)

		if quiet:
			assert not output, output
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

	with tempfile.TemporaryDirectory(prefix='mh-dedicated-race-log-') as temporary:
		root = Path(temporary)

		for name in ('goldbridge.mot', 'redrock.mot'):
			synthetic_course(root / name)

		for mode in ('Normal', 'Elimination') if args.mode == 'both' else (args.mode,):
			qualify(args, root / 'server.cfg', root, mode, mode == 'Elimination')

	print(f'Dedicated race log: append, timestamp, original fields and rotation passed '
		f'for selected mode(s): {args.mode}. Normal also checks abort suppression; '
		'Elimination checks quiet operation.')


if __name__ == '__main__':
	main()
