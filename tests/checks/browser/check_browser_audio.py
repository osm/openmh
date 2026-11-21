#!/usr/bin/env python3
"""Check OpenAL browser audio continuity and suspension with real streams and timers."""

import argparse
from pathlib import Path

from tests.checks.support.browser import run_browser
from tests.checks.support.build import build_executable


def resources(build, scenario):
	target = build_executable(build, f"mh-browser-audio-{scenario}-check")
	wasm = target.with_suffix(".wasm")
	helpers = '<script src="/audio_queue_probe.js"></script>' if scenario == 'queue' else ''
	page = ('<!doctype html><meta charset="utf-8">'
		f'<script src="/{target.name}"></script>{helpers}<script src="/check.js"></script>')
	content = {
		"/": (page.encode(), "text/html"),
		"/" + target.name: (target.read_bytes(), "text/javascript"),
		"/" + wasm.name: (wasm.read_bytes(), "application/wasm"),
		"/check.js": (Path(__file__).with_name(f"audio_{scenario}.js").read_bytes(),
			"text/javascript")}

	if scenario == 'queue':
		for name in ('audio_queue_probe.js', 'audio_queue_worklet.js'):
			content['/' + name] = (Path(__file__).with_name(name).read_bytes(), 'text/javascript')

	return content


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--browser", choices=("chrome", "firefox"), required=True)
	parser.add_argument("--executable")
	parser.add_argument("--scenario", choices=("suspend", "queue"))
	args = parser.parse_args()

	for scenario in (args.scenario,) if args.scenario else ("suspend", "queue"):
		report = run_browser(resources(args.build, scenario), args.output / scenario,
			args.browser, args.executable, audio=True)

		if not report["passed"]:
			raise RuntimeError(report["error"])

		print(f'{args.browser}: {len(report["checks"])} browser audio {scenario} checks passed')


if __name__ == "__main__":
	main()
