#!/usr/bin/env python3
"""Lose and restore a real browser WebGL context while its canvas stays alive.

Checks exact pixels, repeated restoration, resize, and closing presentation
owners both during loss and before their first restored draw. No game assets.
"""

import argparse
from pathlib import Path

from tests.checks.support.browser import run_browser
from tests.checks.support.build import build_executable


def resources(build, renderer):
	name = "mh-webgl-renderer-check" if renderer else "mh-webgl-canvas-recovery-check"
	script = "renderer_recovery.js" if renderer else "webgl_recovery.js"
	target = build_executable(build, name)
	wasm = target.with_suffix(".wasm")
	page = ('<!doctype html><meta charset="utf-8"><canvas id="canvas"></canvas>'
		f'<script src="/{target.name}"></script><script src="/check.js"></script>')

	return {"/": (page.encode(), "text/html"),
		"/" + target.name: (target.read_bytes(), "text/javascript"),
		"/" + wasm.name: (wasm.read_bytes(), "application/wasm"),
		"/check.js": (Path(__file__).with_name(script).read_bytes(),
			"text/javascript")}


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--browser", choices=("chrome", "firefox"), required=True)
	parser.add_argument("--executable")
	parser.add_argument("--renderer", action="store_true",
		help="exercise Glide, Direct3D and PowerVR race recovery")
	args = parser.parse_args()
	report = run_browser(resources(args.build, args.renderer), args.output,
		args.browser, args.executable)

	if not report["passed"]:
		raise RuntimeError(report["error"])

	print(f'{args.browser}: {len(report["checks"])} WebGL recovery checks passed')


if __name__ == "__main__":
	main()
