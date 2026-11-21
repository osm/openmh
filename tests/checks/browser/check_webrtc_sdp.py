#!/usr/bin/env python3
"""Check real browser offers and native ICE-lite answers without game assets."""

import argparse
from pathlib import Path
import subprocess

from tests.checks.support.browser import run_browser
from tests.checks.support.build import build_executable


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--browser", choices=("chrome", "firefox"), required=True)
	parser.add_argument("--executable")
	args = parser.parse_args()
	binary = build_executable(args.build, "test-webrtc-sdp")
	page = '<!doctype html><meta charset="utf-8"><script src="/check.js"></script>'
	resources = {
		"/": (page.encode(), "text/html"),
		"/check.js": (Path(__file__).with_name("webrtc_sdp.js").read_bytes(), "text/javascript"),
	}

	def answer(request):
		family = request["family"]

		if family not in (4, 6):
			raise ValueError("Invalid address family")

		result = subprocess.run([str(binary), f"--answer{family}"],
			input=request["offer"].encode(), capture_output=True,
			check=True, timeout=10)
		resources["/answer"] = (result.stdout, "application/sdp")

	report = run_browser(resources, args.output, args.browser, args.executable,
		timeout=30, input_handler=answer)

	if not report["passed"]:
		raise RuntimeError(report["error"])

	print(f'{args.browser}: native SDP accepted for IPv4 and IPv6')


if __name__ == "__main__":
	main()
