#!/usr/bin/env python3
"""Exercise the C WebGL backend's profile blending/depth rules using pixel probes.

Requires the Emscripten mh-webgl-pipelines-check target and Chrome or Firefox.
Uses private browser storage and a temporary loopback server; no game assets.
"""

import argparse
from pathlib import Path

from tests.checks.support.browser import run_browser
from tests.checks.support.wasm import fixture_resources


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/wasm'))
	parser.add_argument('--output', type=Path, required=True, help='new output directory')
	parser.add_argument('--browser', choices=('chrome', 'firefox'), required=True)
	parser.add_argument('--executable', help='browser executable override')
	args = parser.parse_args()
	resources = fixture_resources(args.build, 'mh-webgl-pipelines-check',
		'createPipelineCheck', '_mh_webgl_pipeline_check')
	report = run_browser(resources, args.output, args.browser, args.executable)

	if not report['passed']:
		raise RuntimeError(report['error'])

	print(f'{args.browser}: {len(report["checks"])} WebGL pipeline pixel probes passed')


if __name__ == '__main__':
	main()
