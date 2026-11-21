"""Two-peer loopback relay for patched race acceptance, with unchanged payloads.

The legacy UDP acceptance reveals the host's per-peer port. The relay forwards
the handshake, then perturbs application datagrams and ACKs in both directions.
"""

import heapq
import select
import socket
import threading
import time


class DatagramRelay:
	def __init__(self, host_port):
		self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		self.socket.bind(('127.0.0.1', 0))
		self.port = self.socket.getsockname()[1]
		self.host = ('127.0.0.1', host_port)
		self.server = self.host
		self.client = None
		self.accepted = False
		self.pending = []
		self.order = 0
		self.last_sent = [0, 0]
		self.stats = [dict(received=0, dropped=0, duplicated=0, delayed=0,
			reordered=0, forwarded=0) for _ in range(2)]
		self.error = None
		self.stopping = threading.Event()
		self.thread = threading.Thread(target=self.run, name='mh-udp-faults')

	def __enter__(self):
		self.thread.start()

		return self

	def __exit__(self, *unused):
		self.stopping.set()
		self.thread.join(timeout=2)
		self.socket.close()
		assert not self.thread.is_alive(), 'UDP relay did not stop'

	def check(self):
		if self.error is not None:
			raise RuntimeError('UDP relay failed') from self.error

	def receive(self, data, source, now):
		if self.client is None:
			# Only the first client's original hello establishes this endpoint.
			assert data == b'\x01\0\x08\0\x66\x66\xa6\x3f', data
			self.client = source

		if source == self.client:
			direction = 0
			target = self.server
		else:
			assert source[0] == '127.0.0.1', source
			direction = 1
			target = self.client

			if not self.accepted:
				assert data[:4] == b'\x02\0\x14\0' and len(data) == 20, data
				self.server = source
			else:
				assert source == self.server, (source, self.server)

		if not self.accepted:
			self.socket.sendto(data, target)

			if direction == 1:
				self.accepted = True

			return

		stats = self.stats[direction]
		stats['received'] += 1
		sequence = stats['received']

		if sequence % 17 == 0:
			stats['dropped'] += 1

			return

		delay = .140 if sequence % 3 == 0 else .010
		stats['delayed'] += 1
		self.schedule(now + delay, data, target, direction, sequence, False)

		if sequence % 11 == 0:
			stats['duplicated'] += 1
			self.schedule(now + delay + .005, data, target, direction, sequence, True)

	def schedule(self, due, data, target, direction, sequence, duplicate):
		self.order += 1
		heapq.heappush(self.pending, (due, self.order, data, target,
			direction, sequence, duplicate))

	def flush(self, now):
		while self.pending and self.pending[0][0] <= now:
			_, _, data, target, direction, sequence, duplicate = heapq.heappop(self.pending)
			self.socket.sendto(data, target)
			stats = self.stats[direction]
			stats['forwarded'] += 1

			if not duplicate and sequence < self.last_sent[direction]:
				stats['reordered'] += 1

			self.last_sent[direction] = max(self.last_sent[direction], sequence)

	def run(self):
		try:
			while not self.stopping.is_set():
				if select.select([self.socket], [], [], .005)[0]:
					data, source = self.socket.recvfrom(65535)
					self.receive(data, source, time.monotonic())

				self.flush(time.monotonic())
		except BaseException as error:
			self.error = error
