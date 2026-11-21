"""The acceptance relay must change delivery, never payload bytes."""

import sys
from pathlib import Path
import socket
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tests.checks.support import udp_faults as MODULE



class RelayTest(unittest.TestCase):
	def endpoint(self):
		endpoint = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		endpoint.bind(('127.0.0.1', 0))
		endpoint.settimeout(2)
		self.addCleanup(endpoint.close)

		return endpoint

	def test_peer_port_and_bidirectional_faults(self):
		listener, server, client = (self.endpoint() for _ in range(3))
		hello = b'\x01\0\x08\0\x66\x66\xa6\x3f'
		accepted = b'\x02\0\x14\0Host' + bytes(12)

		with MODULE.DatagramRelay(listener.getsockname()[1]) as relay:
			address = ('127.0.0.1', relay.port)
			client.sendto(hello, address)
			data, source = listener.recvfrom(100)
			self.assertEqual((data, source), (hello, address))
			server.sendto(accepted, source)
			self.assertEqual(client.recvfrom(100), (accepted, address))

			# The client now reaches the learned server peer socket, not the
			# listener. Delays reorder the fast sequence independently each way.
			for sequence in range(1, 35):
				client.sendto(b'C' + bytes([sequence]), address)
				server.sendto(b'S' + bytes([sequence]), address)

			received = [[], []]
			server.setblocking(False)
			client.setblocking(False)
			deadline = time.monotonic() + 2

			while time.monotonic() < deadline:
				relay.check()

				for endpoint, rows in zip((server, client), received):
					try:
						data, source = endpoint.recvfrom(100)
					except BlockingIOError:
						continue

					self.assertEqual(source, address)
					rows.append(data)

				if all(len(rows) == 35 for rows in received):
					break

				time.sleep(.002)

			for prefix, rows, stats in zip((b'C', b'S'), received, relay.stats):
				expected = [prefix + bytes([i]) for i in range(1, 35) if i % 17]
				expected += [prefix + bytes([i]) for i in (11, 22, 33)]
				self.assertCountEqual(rows, expected)
				self.assertNotEqual(rows, sorted(rows))
				self.assertEqual(stats['dropped'], 2)
				self.assertEqual(stats['duplicated'], 3)
				self.assertGreater(stats['reordered'], 0)

			relay.check()

		self.assertFalse(relay.thread.is_alive())


if __name__ == '__main__':
	unittest.main()
