/* Small synthetic probes for conventions that source translation cannot prove. */
function checkShaderDrawing(gl, programs, require, report) {
	const size = 64;
	const framebuffer = gl.createFramebuffer();
	gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
	const color = gl.createTexture();
	gl.bindTexture(gl.TEXTURE_2D, color);
	gl.texImage2D(gl.TEXTURE_2D,
		0,
		gl.RGBA8,
		size,
		size,
		0,
		gl.RGBA,
		gl.UNSIGNED_BYTE,
		null);
	gl.framebufferTexture2D(
		gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, color, 0);
	const depth = gl.createRenderbuffer();
	gl.bindRenderbuffer(gl.RENDERBUFFER, depth);
	gl.renderbufferStorage(
		gl.RENDERBUFFER, gl.DEPTH_COMPONENT32F, size, size);
	gl.framebufferRenderbuffer(
		gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depth);
	require(gl.checkFramebufferStatus(gl.FRAMEBUFFER) ===
			gl.FRAMEBUFFER_COMPLETE,
		'RGBA8/D32F framebuffer');
	gl.bindVertexArray(gl.createVertexArray());
	gl.viewport(0, 0, size, size);
	gl.disable(gl.CULL_FACE);
	gl.disable(gl.DITHER);

	function clear() {
		gl.disable(gl.BLEND);
		gl.enable(gl.DEPTH_TEST);
		gl.depthFunc(gl.LEQUAL);
		gl.depthMask(true);
		gl.clearColor(0, 0, 0, 1);
		gl.clearDepth(1);
		gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
	}

	function pixel(x = 32, y = 24) {
		const bytes = new Uint8Array(4);
		gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
		return Array.from(bytes);
	}

	function expect(name, actual, expected, tolerance = 0) {
		require(expected.every(
				(value, i) => Math.abs(value - actual[i]) <=
					tolerance),
			name + ': ' + actual + ', expected ' + expected);
		report.checks.push({name, pixel: actual, tolerance});
	}

	function block(program, name, slot, data) {
		const index = gl.getUniformBlockIndex(program, name);
		require(index !== gl.INVALID_INDEX,
			'Missing uniform block ' + name);
		gl.uniformBlockBinding(program, index, slot);
		const buffer = gl.createBuffer();
		gl.bindBuffer(gl.UNIFORM_BUFFER, buffer);
		gl.bufferData(gl.UNIFORM_BUFFER,
			new Float32Array(data),
			gl.STREAM_DRAW);
		gl.bindBufferBase(gl.UNIFORM_BUFFER, slot, buffer);
	}

	function texture(bytes, width = 1, height = 1) {
		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, gl.createTexture());
		gl.texImage2D(gl.TEXTURE_2D,
			0,
			gl.RGBA8,
			width,
			height,
			0,
			gl.RGBA,
			gl.UNSIGNED_BYTE,
			new Uint8Array(bytes));
		gl.texParameteri(
			gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(
			gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(
			gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(
			gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
	}

	function vertices(data, widths) {
		gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
		gl.bufferData(gl.ARRAY_BUFFER,
			new Float32Array(data),
			gl.STREAM_DRAW);
		const stride = widths.reduce((a, b) => a + b, 0) * 4;
		let offset = 0;
		widths.forEach((width, index) => {
			gl.enableVertexAttribArray(index);
			gl.vertexAttribPointer(
				index, width, gl.FLOAT, false, stride, offset);
			offset += width * 4;
		});
	}

	function triangle(program, options = {}) {
		gl.useProgram(program);
		const camera = new Float32Array(60);
		for (const base of [0, 16, 32])
			for (const diagonal of [0, 5, 10, 15])
				camera[base + diagonal] = 1;
		camera[26] =
			-1; // Positive input Z becomes positive camera distance.
		camera.set([1, size, size, options.powervr ? 1 : 0], 48);
		camera.set([0.2, 0.3, 0.4, 1], 52);
		block(program, 'Camera', 0, camera);
		block(program, 'Material', 1, [
			options.environment || 0,
			1,
			options.fog || 0,
			options.alpha || 0,
			0,
			0,
			options.flat || 0,
			1
		]);
		const data = [];
		const corners = [[-0.8, -0.8], [0.8, -0.8], [0, 0.8]];
		for (const i of options.order || [0, 1, 2]) {
			const [x, y] = corners[i];
			const z = options.z ? options.z[i] : 0.4;
			const rgb =
				options.colors ? options.colors[i] : [1, 1, 1];
			data.push(x,
				y,
				z,
				0,
				0,
				1,
				0.5,
				0.5,
				...rgb,
				options.w ? options.w[i] : 1,
				options.powervr ? 0 : 1);
		}
		vertices(data, [3, 3, 2, 3, 1, 1]);
		for (const index of [6, 7]) {
			gl.disableVertexAttribArray(index);
			gl.vertexAttrib2f(index, 0.5, 0.5);
		}
		texture(options.texel || [255, 255, 255, 255]);
		gl.drawArrays(gl.TRIANGLES, 0, 3);
	}

	for (const profile of ['glide', 'direct3d']) {
		const scene = programs[profile + '/' + profile];
		for (const z of [-0.2, 0.2, 1.2]) {
			clear();
			triangle(scene, {z: [z, z, z]});
			expect(profile + ' clip depth ' + z,
				pixel(),
				z === 0.2 ? [255, 255, 255] : [0, 0, 0]);
		}
		const gradient = {
			colors: [[1, 0, 0], [0, 1, 0], [0, 0, 1]],
			z: [0.1, 0.45, 0.75],
			fog: 1
		};
		clear();
		triangle(scene, gradient);
		const positions = [[24, 24], [40, 24], [32, 40]];
		const reference = positions.map(([x, y]) => pixel(x, y));
		clear();
		triangle(scene, {...gradient, w: [0.5, 2, 4]});
		positions.forEach(
			([x, y], i) => expect(
				profile + ' screen-linear color/fog ' + i,
				pixel(x, y),
				reference[i],
				1));

		for (const [alpha, cutoff, visible] of [[0, 1, false],
			     [127, 1, true],
			     [127, 2, false],
			     [128, 2, true]]) {
			clear();
			triangle(scene,
				{alpha: cutoff, texel: [255, 255, 255, alpha]});
			expect(profile + ' alpha ' + alpha + '/' + cutoff,
				pixel(),
				visible ? [255, 255, 255] : [0, 0, 0]);
		}
		for (const powervr of [false, true]) {
			const program =
				powervr ? programs['powervr/powervr'] : scene;
			clear();
			triangle(program, {
				powervr,
				z: [0.2, 0.2, 0.2],
				w: [4, 4, 4],
				texel: [255, 0, 0, 255]
			});
			triangle(program, {
				powervr,
				z: [0.8, 0.8, 0.8],
				w: [1, 1, 1],
				texel: [0, 255, 0, 255]
			});
			expect(powervr ? 'PowerVR reciprocal-W depth'
				       : 'ordinary projected depth',
				pixel(),
				powervr ? [0, 255, 0] : [255, 0, 0]);
		}
		clear();
		triangle(programs[profile + '_glenz/' + profile], {
			environment: 1,
			w: [0.5, 2, 4],
			texel: [255, 0, 0, 255]
		});
		gl.enable(gl.BLEND);
		gl.blendFunc(gl.ONE, gl.ONE);
		triangle(programs[profile + '_glenz/' + profile], {
			environment: 2,
			w: [0.5, 2, 4],
			texel: [0, 255, 0, 255]
		});
		expect(profile + ' Glenz coplanar reflection/Phong',
			pixel(),
			[255, 255, 0]);

		clear();
		triangle(scene,
			{flat: 1, colors: [[1, 0, 0], [0, 1, 0], [0, 0, 1]]});
		expect(profile + ' WebGL last-vertex flat color',
			pixel(),
			[0, 0, 255]);
		clear();
		triangle(scene, {
			flat: 1,
			colors: [[1, 0, 0], [0, 1, 0], [0, 0, 1]],
			order: [1, 2, 0]
		});
		expect(profile +
				' cyclic ordering preserves native first-vertex flat color',
			pixel(),
			[255, 0, 0]);
	}
	report.flatConvention =
		'WebGL uses the last provoking vertex; a future backend must preserve the native flat-color policy';

	clear();
	gl.disable(gl.DEPTH_TEST);
	const ui = programs['ui/ui'];
	gl.useProgram(ui);
	block(ui, 'Viewport', 0, [size, size, 0, 0]);
	texture(
		[
			255,
			0,
			0,
			255,
			0,
			255,
			0,
			255,
			0,
			0,
			255,
			255,
			255,
			255,
			255,
			255
		],
		2,
		2);
	const quad = [];
	for (const [x, y] of [[0, 0], [1, 0], [0, 1], [1, 0], [1, 1], [0, 1]])
		quad.push(x * size, y * size, x, y, 1, 1, 1, 1);
	vertices(quad, [2, 2, 4]);
	for (const index of [3, 4, 5])
		gl.disableVertexAttribArray(index);
	gl.drawArrays(gl.TRIANGLES, 0, 6);
	expect('UI top-left texel', pixel(16, 48), [255, 0, 0]);
	expect('UI top-right texel', pixel(48, 48), [0, 255, 0]);
	expect('UI bottom-left texel', pixel(16, 16), [0, 0, 255]);
	expect('UI bottom-right texel', pixel(48, 16), [255, 255, 255]);

	// Framebuffer texels use the opposite V orientation from top-down uploads.
	// Sampling them with asset UVs therefore requires a V flip.
	gl.bindFramebuffer(gl.FRAMEBUFFER, null);
	gl.bindTexture(gl.TEXTURE_2D, color);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
	gl.drawArrays(gl.TRIANGLES, 0, 6);
	expect('unadjusted framebuffer texture is inverted',
		pixel(16, 48),
		[0, 0, 255]);
	for (let i = 0; i < quad.length; i += 8)
		quad[i + 3] = 1 - quad[i + 3];
	vertices(quad, [2, 2, 4]);
	gl.drawArrays(gl.TRIANGLES, 0, 6);
	expect('framebuffer V correction restores top-left',
		pixel(16, 48),
		[255, 0, 0]);
	require(gl.getError() === gl.NO_ERROR,
		'WebGL error during drawing probes');
}
