#!/usr/bin/env python3
"""Check OpenMH vertex lighting with synthetic pixel probes, without game assets."""

import argparse
from pathlib import Path

from tests.checks.support.browser import run_browser
from tests.checks.support.wasm import fixture_resources


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/wasm'))
	parser.add_argument('--output', type=Path, required=True)
	parser.add_argument('--browser', choices=('chrome', 'firefox'), required=True)
	args = parser.parse_args()
	resources = fixture_resources(args.build, 'mh-openmh-render-check',
		'createOpenMHCheck', '_mh_openmh_render_check')
	report = run_browser(resources, args.output, args.browser)

	if not report['passed']:
		raise RuntimeError(report['error'])

	print(f'{args.browser}: {len(report["checks"])} OpenMH lighting checks passed')


if __name__ == '__main__':
	main()
