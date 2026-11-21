"""Write small synthetic parser seeds; no game assets are used."""

from io import BytesIO
from pathlib import Path
import struct
import sys
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile, ZipInfo
import zlib


def ass_seeds():
	def package(members):
		buffer = BytesIO()

		with ZipFile(buffer, "w") as archive:
			for path, data, method in members:
				archive.writestr(ZipInfo(path), data, compress_type=method)

		return buffer.getvalue()

	stored = ("readme.txt", b"OpenMH\n", ZIP_STORED)
	compressed = ("Data/nested/sample.bin", bytes(range(32)) * 8, ZIP_DEFLATED)

	return {
		"empty": package([]),
		"stored": package([stored]),
		"deflated": package([compressed]),
		"mixed": package([("Data/", b"", ZIP_STORED), compressed, stored,
			("Data/empty.bin", b"", ZIP_DEFLATED)]),
	}


def webrtc_seeds():
	stun = struct.pack("!HHI12s", 1, 16, 0x2112A442, b"synthetic-id")
	stun += struct.pack("!HHI", 0x24, 4, 123)
	fingerprint = zlib.crc32(stun) ^ 0x5354554E
	stun += struct.pack("!HHI", 0x8028, 4, fingerprint)
	sctp = bytearray(struct.pack("!HHII", 5000, 5000, 123, 0)
		+ struct.pack("!BBH", 11, 0, 4))
	crc = 0xFFFFFFFF

	for value in sctp:
		crc ^= value

		for _ in range(8):
			crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)

	sctp[8:12] = struct.pack("<I", crc ^ 0xFFFFFFFF)
	fingerprint = ":".join(f"{i:02X}" for i in range(32))
	sdp = ("v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
		"m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
		"a=ice-ufrag:test\r\na=ice-pwd:abcdefghijklmnopqrstuv\r\n"
		f"a=fingerprint:sha-256 {fingerprint}\r\na=setup:actpass\r\n"
		"a=mid:0\r\na=sctp-port:5000\r\n").encode()

	return {"stun": stun, "sctp": sctp, "sdp": sdp, "truncated": b"\0\x01"}


def sctp_seeds():
	def command(operation, payload=b"", kind=0, flags=0, elapsed=1):
		return struct.pack("!BBBHH", operation, kind, flags, len(payload), elapsed) + payload

	def data(tsn, flags, payload):
		return command(0, struct.pack("!IHHI", tsn, 0, 0, 53) + payload, flags=flags)

	send = command(1, bytes(range(256)) * 32)
	sack = command(0, struct.pack("!IIHH", 0xFFFFFFF3, 65536, 0, 0), kind=3)

	return {
		"fragments": data(0xFFFFFFF2, 5, b"end") + data(0xFFFFFFF0, 6, b"begin")
		+ data(0xFFFFFFF1, 4, b"middle") + command(3),
		"abandon": data(0xFFFFFFF0, 6, b"begin") + data(0xFFFFFFF2, 5, b"end")
		+ command(0, struct.pack("!I", 0xFFFFFFF2), kind=192),
		"timeout": send + command(2, elapsed=3001) + command(2, elapsed=6001),
		"sack": send + sack + command(0, struct.pack("!IIHH", 0xFFFFFFF7, 65536, 0, 0), kind=3),
		"fast-loss": send + b"".join(command(0, struct.pack("!IIHHHH", 0xFFFFFFEF, 65536, 1, 0, 2, end), kind=3)
			for end in (2, 3, 4)),
		"heartbeat": command(0, struct.pack("!HH4s", 1, 8, b"ping"), kind=4),
		"unknown": command(0, b"test", kind=0xC1),
	}


def write_seeds(root):
	seeds = {
		"ass": ass_seeds(),
		"webrtc": webrtc_seeds(),
		"sctp": sctp_seeds(),
		"signaling": {
			"post": b"POST / HTTP/1.1\r\nHost: game.invalid\r\nContent-Type: application/sdp\r\n"
			b"Content-Length: 5\r\nOrigin: https://game.invalid\r\nX-Real-IP: 2001:db8::1\r\n\r\noffer",
			"options": b"OPTIONS / HTTP/1.1\r\nHost: game.invalid\r\nOrigin: https://game.invalid\r\n"
			b"Access-Control-Request-Method: POST\r\nAccess-Control-Request-Headers: content-type\r\n\r\n",
			"duplicate-length": b"POST / HTTP/1.1\r\nHost: game.invalid\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\noffer",
		},
		"network": {
			"shared-hello": struct.pack("<4sB3xII", b"OMH1", 1, 12, 0),
			"shared-accept": struct.pack("<4sB3xII16s", b"OMH1", 2, 12, 34,
				b"Fixture"),
			"shared-data": struct.pack("<4sB3xII", b"OMH1", 4, 12, 34)
			+ b"\0payload",
			"shared-reject": struct.pack("<4sB3xII", b"OMH1", 3, 12, 0)
			+ b"Rejected\0",
			"ping": struct.pack("<HHI", 10, 8, 123),
			"chat": struct.pack("<HHBI", 7, 12, 3, 0xE06820) + b"hi\0",
			"truncated": b"\x07\0\xff\xff",
		},
		"tga": {
			"rgb": struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0,
				0, 0, 1, 1, 24, 0x20) + b"\x11\x22\x33",
			"indexed": struct.pack("<BBBHHBHHHHBB", 0, 1, 1, 0, 2, 24,
				0, 0, 2, 1, 8, 0x20)
			+ b"\0\0\0\xff\xff\xff\0\x01",
			"gray": struct.pack("<BBBHHBHHHHBB", 0, 0, 3, 0, 0, 0,
				0, 0, 1, 1, 8, 0) + b"\x80",
			"bgr555": struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0,
				0, 0, 1, 1, 16, 1) + b"\xff\xff",
			"truncated": b"\0\0\x02",
		},
		"settings": {
			"names": b"PlayerName Player\nTeamName TEAM\nCarName Car Alpha\n"
			b"TrackName Track Alpha\nCarHorn Honker1\n"
			b"ControlName Keyboard\n",
			"numbers": b"ScreenDimension 1280 1024\nBrightness 100 100 100\n"
			b"ViewDistance 1000\nMaxFPS 60\n",
			"range": b"ViewDistance 1e300\n",
			"long-name": b"PlayerName " + b"x" * 1024 + b"\n",
		},
	}

	for parser, entries in seeds.items():
		directory = root / parser
		directory.mkdir(parents=True, exist_ok=True)

		for name, data in entries.items():
			(directory / name).write_bytes(data)


if __name__ == "__main__":
	write_seeds(Path(sys.argv[1]))
