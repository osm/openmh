"""Serve a modular Emscripten C fixture and collect its result."""

from tests.checks.support.build import build_executable


def fixture_resources(build, target, factory, entrypoint, *, asynchronous=False):
	executable = build_executable(build, target)
	wasm = executable.with_suffix('.wasm')
	invocation = (f"await game.ccall('{entrypoint.removeprefix('_')}', "
		"'number', [], [], {async: true})" if asynchronous
		else f"game.{entrypoint}()")
	page = '''<!doctype html><meta charset="utf-8">
<canvas id="canvas" width="4" height="4"></canvas>
<script src="/SCRIPT"></script><script>
const checks = [], errors = [];
function report(passed, error) {
    return fetch('/report', {method:'POST', body:JSON.stringify({passed, checks, error})});
}
FACTORY({canvas:document.getElementById('canvas'),
    print:text=>checks.push(text), printErr:text=>errors.push(text)
}).then(async game=> {
    const status = INVOCATION;
    return report(status === 0, errors.join('\\n') || 'WebGL fixture failed');
}).catch(error=>report(false, error.stack || String(error)));
</script>'''.replace('SCRIPT', executable.name).replace('FACTORY', factory).replace('INVOCATION', invocation)

	return {'/': (page.encode(), 'text/html'),
		'/' + executable.name: (executable.read_bytes(), 'text/javascript'),
		'/' + wasm.name: (wasm.read_bytes(), 'application/wasm')}
