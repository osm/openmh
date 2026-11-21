/* Shader-only WebGL 2 validation. No game assets or browser backend are used. */
(async function() {
	const report = {passed: false, programs: [], checks: []};
	function require(value, message) {
		if (!value)
			throw new Error(message);
	}
	try {
		const gl =
			document.getElementById('canvas').getContext('webgl2', {
				alpha: false,
				antialias: false,
				preserveDrawingBuffer: true
			});
		require(gl,
			'WebGL 2 context is unavailable in this browser configuration');
		const debug = gl.getExtension('WEBGL_debug_renderer_info');
		report.renderer = debug
			? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL)
			: gl.getParameter(gl.RENDERER);
		report.version = gl.getParameter(gl.VERSION);
		report.browser = navigator.userAgent;
		report.limits = {};
		for (const name of ['MAX_VERTEX_ATTRIBS',
			     'MAX_VERTEX_OUTPUT_COMPONENTS',
			     'MAX_FRAGMENT_INPUT_COMPONENTS',
			     'MAX_VERTEX_UNIFORM_BLOCKS',
			     'MAX_FRAGMENT_UNIFORM_BLOCKS',
			     'MAX_UNIFORM_BLOCK_SIZE',
			     'MAX_UNIFORM_BUFFER_BINDINGS',
			     'MAX_TEXTURE_IMAGE_UNITS']) {
			report.limits[name] = gl.getParameter(gl[name]);
		}
		report.precision = {};
		for (const [stage, name] of [[gl.VERTEX_SHADER, 'vertex'],
			     [gl.FRAGMENT_SHADER, 'fragment']]) {
			const p = gl.getShaderPrecisionFormat(
				stage, gl.HIGH_FLOAT);
			report.precision[name] = {
				min: p.rangeMin,
				max: p.rangeMax,
				bits: p.precision
			};
			require(p.precision >= 23,
				name + ' highp precision is below binary32');
		}
		const shaders = {};
		for (const [name, source] of Object.entries(fixture.sources)) {
			const shader = gl.createShader(name.endsWith('_vert')
					? gl.VERTEX_SHADER
					: gl.FRAGMENT_SHADER);
			gl.shaderSource(shader, source);
			gl.compileShader(shader);
			require(gl.getShaderParameter(
					shader, gl.COMPILE_STATUS),
				name + ': ' + gl.getShaderInfoLog(shader));
			shaders[name] = shader;
		}
		const programs = {};
		for (const [vertex, fragment] of fixture.pairs) {
			const name = vertex + '/' + fragment;
			const program = gl.createProgram();
			gl.attachShader(program, shaders[vertex + '_vert']);
			gl.attachShader(program, shaders[fragment + '_frag']);
			gl.linkProgram(program);
			require(gl.getProgramParameter(program, gl.LINK_STATUS),
				name + ': ' + gl.getProgramInfoLog(program));
			programs[name] = program;
			const blocks = [];
			for (let index = 0;
				index < gl.getProgramParameter(program,
						gl.ACTIVE_UNIFORM_BLOCKS);
				index++) {
				const indices = gl.getActiveUniformBlockParameter(
					program,
					index,
					gl.UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES);
				const offsets = gl.getActiveUniforms(
					program, indices, gl.UNIFORM_OFFSET);
				const strides = gl.getActiveUniforms(program,
					indices,
					gl.UNIFORM_MATRIX_STRIDE);
				blocks.push({
					name: gl.getActiveUniformBlockName(
						program, index),
					size: gl.getActiveUniformBlockParameter(
						program,
						index,
						gl.UNIFORM_BLOCK_DATA_SIZE),
					members: Array.from(indices, (i, n) => ({
						name: gl.getActiveUniform(
								program, i)
							.name,
						offset: offsets[n],
						matrixStride: strides[n]
					}))
				});
			}
			report.programs.push({name, blocks});
		}
		require(gl.getError() === gl.NO_ERROR,
			'WebGL error during program validation');
		checkShaderDrawing(gl, programs, require, report);
		report.passed = true;
	} catch (error) {
		report.error = error.message;
	}
	document.getElementById('status').textContent =
		JSON.stringify(report, null, 2);
	await fetch('/report', {method: 'POST', body: JSON.stringify(report)});
})();
