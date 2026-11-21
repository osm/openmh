#!/usr/bin/env python3
"""Check browser ICE/DTLS, SCTP delivery or the complete native provider."""

import argparse
import ipaddress
import os
from pathlib import Path
import select
import subprocess
import time

from tests.checks.support.browser import run_browser
from tests.checks.support.build import build_executable


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--browser", choices=("chrome", "firefox"), required=True)
	parser.add_argument("--executable")
	mode = parser.add_mutually_exclusive_group()
	mode.add_argument("--data", action="store_true", help="Also exchange fragmented SCTP messages")
	mode.add_argument("--provider", action="store_true", help="Check two peers through the native provider")
	parser.add_argument("--idle-seconds", type=int, default=0,
		help="Provider check only: leave channels idle to exercise ICE keepalives")
	parser.add_argument("--address", type=ipaddress.IPv4Address, default="127.0.0.1",
		help="Local native peer address; use a LAN address for Firefox")
	args = parser.parse_args()

	if not 0 <= args.idle_seconds <= 40 or (args.idle_seconds and not args.provider):
		parser.error("--idle-seconds requires --provider and a value from 0 to 40")

	binary = build_executable(args.build, "test-webrtc-provider-peer" if args.provider else
		"test-webrtc-browser-peer")
	script = "webrtc_provider.js" if args.provider else ("webrtc_sctp.js" if args.data else "webrtc_dtls.js")
	expected = b"PROVIDER VERIFIED" if args.provider else (b"SCTP VERIFIED" if args.data else b"DTLS VERIFIED")
	page = '<!doctype html><meta charset="utf-8"><script src="/check.js"></script>'
	resources = {
		"/": (page.encode(), "text/html"),
		"/check.js": (Path(__file__).with_name(script).read_bytes(), "text/javascript"),
		"/idle": (str(args.idle_seconds * 1000).encode(), "text/plain"),
	}
	native = None

	def request(message):
		nonlocal native

		if message.get("verify"):
			if native is None:
				raise RuntimeError("Native peer was not started")

			output, errors = native.communicate(timeout=10)
			(args.output / "native.log").write_bytes(output + errors)

			if native.returncode or expected not in output:
				raise RuntimeError(f"Native peer failed: {errors.decode(errors='replace')}")

			return

		if native is not None:
			raise RuntimeError("Native peer already started")

		command = [str(binary), str(args.address)] + (["--data"] if args.data else [])
		native = subprocess.Popen(command, stdin=subprocess.PIPE,
			stdout=subprocess.PIPE, stderr=subprocess.PIPE)
		offers = message["offers"] if args.provider else [message["offer"]]

		if len(offers) != (2 if args.provider else 1):
			raise ValueError("Unexpected offer count")

		native.stdin.write(b"\0".join(offer.encode() for offer in offers))
		native.stdin.close()
		native.stdin = None
		reply = bytearray()
		deadline = time.monotonic() + 10
		marker = b"--READY--\n"

		while marker not in reply:
			remaining = deadline - time.monotonic()

			if remaining <= 0 or not select.select([native.stdout], [], [], remaining)[0]:
				raise RuntimeError("Native SDP answer timed out")

			chunk = os.read(native.stdout.fileno(), 4096)

			if not chunk:
				raise RuntimeError("Native peer exited before producing SDP")

			reply.extend(chunk)

			if len(reply) > 8192:
				raise RuntimeError("Native SDP answer too large")

		answers = bytes(reply).split(marker)[0].split(b"\0")

		if len(answers) != len(offers):
			raise RuntimeError("Unexpected native answer count")

		for index, answer in enumerate(answers):
			resources[f"/answer{index}" if args.provider else "/answer"] = (answer, "application/sdp")

	try:
		report = run_browser(resources, args.output, args.browser, args.executable,
			timeout=75 if args.provider else 30, input_handler=request)

		if not report["passed"]:
			raise RuntimeError(report["error"])

		print(f'{args.browser}: ' + ('native provider lifecycle and reliable delivery passed' if args.provider else
			('SCTP message exchange passed' if args.data else
				'ICE nomination, pinned DTLS handshake and SCTP INIT passed')))
	finally:
		if native is not None and native.poll() is None:
			try:
				native.wait(timeout=1)
			except subprocess.TimeoutExpired:
				native.terminate()

				try:
					native.wait(timeout=5)
				except subprocess.TimeoutExpired:
					native.kill()
					native.wait(timeout=5)

		if native is not None and not (args.output / "native.log").exists():
			output, errors = native.communicate(timeout=5)
			(args.output / "native.log").write_bytes(output + errors)


if __name__ == "__main__":
	main()
