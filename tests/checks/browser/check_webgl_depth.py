#!/usr/bin/env python3
"""Exercise the C WebGL backend's queued depth queries and visibility history.

Requires the Emscripten mh-webgl-depth-check target and Chrome or Firefox.
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
	resources = fixture_resources(args.build, 'mh-webgl-depth-check',
		'createDepthCheck', '_mh_webgl_depth_check')
	page, mime = resources['/']
	page = page.replace(b'.then(game=> {', b'.then(async game=> {')
	page = page.replace(b'    return report(status === 0,', b'''    if (status === 0) {
        try {
            if (game._mh_webgl_depth_poll_check(0) !== 0) throw new Error('Cannot start async depth check');
            const deadline = performance.now() + 5000;
            let pending = 1;
            while (pending === 1) {
                if (performance.now() > deadline) throw new Error('Async depth check timed out');
                await new Promise(resolve => setTimeout(resolve, 20));
                pending = game._mh_webgl_depth_poll_check(1);
            }
            if (pending !== 0) throw new Error('Async depth poll failed');
        } finally {
            if (game._mh_webgl_depth_poll_check(2) !== 0) throw new Error(errors.join('\\n'));
        }
    }
    return report(status === 0,''')
	resources['/'] = (page, mime)
	report = run_browser(resources, args.output, args.browser, args.executable)

	if not report['passed']:
		raise RuntimeError(report['error'])

	print(f'{args.browser}: {len(report["checks"])} WebGL depth checks passed')


if __name__ == '__main__':
	main()
