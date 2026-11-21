#!/usr/bin/env python3
"""Check browser lifecycle event ordering, overlapping causes and resume retries.

Uses a controlled suspension callback and synthetic page/visibility events.
Rendered gameplay and WebGL recovery are outside this check's scope.
"""

import argparse
from pathlib import Path

from tests.checks.support.browser import run_browser


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--browser", choices=("chrome", "firefox"), required=True)
	parser.add_argument("--executable")
	args = parser.parse_args()
	page = ('<!doctype html><meta charset="utf-8"><canvas id="canvas"></canvas>'
		'<script src="/lifecycle.js"></script><script src="/check.js"></script>')
	resources = {"/": (page.encode(), "text/html"),
		"/lifecycle.js": ((args.build / "lifecycle.js").read_bytes(), "text/javascript"),
		"/check.js": (Path(__file__).with_name("browser_lifecycle.js").read_bytes(),
			"text/javascript")}
	report = run_browser(resources, args.output, args.browser, args.executable)

	if not report["passed"]:
		raise RuntimeError(report["error"])

	print(f'{args.browser}: {len(report["checks"])} lifecycle event checks passed')


if __name__ == "__main__":
	main()
