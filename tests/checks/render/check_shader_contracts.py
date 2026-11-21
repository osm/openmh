#!/usr/bin/env python3
"""Compare embedded SDL shader contracts with compiled artifacts and reflection.

Requires the mh-shader-contract fixture and SPIRV-Cross/SPIRV-Tools. Optional
Metal and WebGL outputs are checked when generated. This does not run either
platform's graphics backend or establish cross-platform image equivalence.
"""

from tests.checks.support.build import build_executable
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


MATERIAL_FAMILIES = ('material', 'direct3d', 'powervr')
PAIRS = tuple((vertex, profile) for profile in MATERIAL_FAMILIES
	for vertex in (profile, profile + '_glenz')) + (
	('ui', 'ui'), ('tron', 'tron'), ('retro', 'retro'), ('depth', 'depth'), ('openmh', 'material'))


def require(condition, message):
	if not condition:
		raise RuntimeError(message)


def run(*command):
	result = subprocess.run(list(map(str, command)), capture_output=True,
		text=True, timeout=60)
	require(result.returncode == 0,
		f'{command[0]} exited {result.returncode}: {result.stdout}{result.stderr}')

	return result.stdout


def native_contract(text):
	programs, blocks, inputs, buffers = {}, {}, {}, {}

	for line in text.splitlines():
		kind, *fields = line.split()

		if kind == 'program':
			name, target, stage, entrypoint, uniforms, samplers = fields
			key = (name, target)
			require(key not in programs, f'Duplicate native program: {key}')
			programs[key] = dict(stage=stage, entrypoint=entrypoint,
				uniforms=int(uniforms), samplers=int(samplers))
		elif kind == 'block':
			name, size = fields
			blocks[name] = dict(size=int(size), members={})
		elif kind == 'member':
			block, name, offset = fields
			blocks[block]['members'][name] = int(offset)
		elif kind == 'buffer':
			name, slot, pitch = fields
			buffers.setdefault(name, {})[int(slot)] = int(pitch)
		elif kind == 'input':
			name, location, data_type, offset, slot = fields
			attributes = inputs.setdefault(name, {})
			require(int(location) not in attributes, f'{name}: duplicate location')
			attributes[int(location)] = (data_type, int(offset), int(slot))
		else:
			raise RuntimeError(f'Unknown native contract record: {line}')

	return programs, blocks, inputs, buffers


def check_inputs(name, reflection, inputs, buffers):
	native = inputs.get(name, {})
	reflected = {v['location']: v['type'] for v in reflection.get('inputs', [])}
	require({location: attr[0] for location, attr in native.items()} == reflected,
		f'{name}: C pipeline inputs differ from shader inputs')
	used = set()

	for data_type, offset, slot in native.values():
		size = {'float': 4, 'vec2': 8, 'vec3': 12, 'vec4': 16}[data_type]
		require(slot in buffers[name] and offset + size <= buffers[name][slot],
			f'{name}: attribute outside vertex buffer stride')
		covered = {(slot, n) for n in range(offset, offset + size)}
		require(not used.intersection(covered), f'{name}: overlapping vertex attributes')
		used.update(covered)


def check_blocks(name, reflection, native):
	for block in reflection.get('ubos', []):
		members = reflection['types'][block['type']]['members']

		if block['name'] == 'Lighting':
			require(block['block_size'] == native['Lighting']['size'], f'{name}: lighting size')
			require([(m['name'], m['offset']) for m in members] ==
				[('ambient_count', 0), ('brightness_specular', 16), ('tint', 32), ('lights', 48)],
				f'{name}: lighting offsets')
			lights = members[-1]
			require(lights.get('array') == [64] and lights.get('array_stride') == 48,
				f'{name}: light array layout')
			fields = reflection['types'][lights['type']]['members']
			require([(m['name'], m['type'], m['offset']) for m in fields] ==
				[('direction', 'vec4', 0), ('color', 'vec4', 16), ('view', 'vec4', 32)],
				f'{name}: light layout')
			continue

		if block['name'] == 'Surface':
			expected = native['Vector']
		elif block['name'] == 'Camera':
			expected = native['Camera']
		elif any(name == vertex + '_vert' for vertex, fragment in PAIRS
			if fragment in MATERIAL_FAMILIES):
			expected = native['SceneMaterial']
		else:
			expected = native['Vector']

		require(block['block_size'] == expected['size'], f'{name}: uniform size')

		if expected['members']:
			offsets = {member['name']: member['offset'] for member in members}
			require(offsets == expected['members'], f'{name}: uniform offsets')
		else:
			require(len(members) == 1 and members[0]['offset'] == 0 and
				members[0]['type'] == 'vec4', f'{name}: single-vector uniform')

		for member in members:
			expected_type = 'mat4' if member['name'] in ('matrix', 'view', 'model') else 'vec4'
			require(member['type'] == expected_type, f'{name}: uniform type')

			if member['type'] == 'mat4':
				require(member['matrix_stride'] == 16 and
					not member.get('row_major', False), f'{name}: matrix layout')


def check_reflection(name, metadata, reflection, blocks):
	stage = metadata['stage']
	require(reflection['entryPoints'] == [{'name': 'main', 'mode': stage}],
		f'{name}: entry point or stage')
	uniforms = reflection.get('ubos', [])
	textures = reflection.get('textures', [])
	require(len(uniforms) == metadata['uniforms'], f'{name}: uniform count')
	require(len(textures) == metadata['samplers'], f'{name}: sampler count')
	uniform_set = 1 if stage == 'vert' else 3
	require(sorted((u['set'], u['binding']) for u in uniforms) ==
		[(uniform_set, i) for i in range(metadata['uniforms'])],
		f'{name}: uniform bindings')
	require([(t['set'], t['binding'], t['type']) for t in textures] ==
		([(2, 0, 'sampler2D')] if metadata['samplers'] else []),
		f'{name}: texture bindings')

	for resource in ('ssbos', 'images', 'separate_images', 'separate_samplers',
		'push_constants'):
		require(not reflection.get(resource), f'{name}: unexpected {resource}')

	check_blocks(name, reflection, blocks)


def check_metal(name, source, metadata):
	require(re.search(r'\b' + re.escape(metadata['entrypoint']) + r'\(', source),
		f'{name}: Metal entry point')

	for resource, count in (('buffer', metadata['uniforms']),
		('texture', metadata['samplers']),
		('sampler', metadata['samplers'])):
		slots = sorted(int(n) for n in re.findall(
			r'\[\[' + resource + r'\((\d+)\)\]\]', source))
		require(slots == list(range(count)), f'{name}: Metal {resource} slots')


def check_webgl_limits(name, reflection, stage):
	# GLES 3.0.6 tables 6.31-6.33 and GLSL ES 3.00 section 7.3.
	# Count each scalar/vector as a whole slot and reserve one built-in slot
	# for position/FragCoord. This is stricter than packing scalar varyings.
	varyings = reflection.get('outputs' if stage == 'vert' else 'inputs', [])
	require(all(v['type'] in ('float', 'vec2', 'vec3', 'vec4') and not v.get('array')
		for v in varyings), f'{name}: update varying-budget accounting')
	vectors = len(varyings) + 1
	require(vectors <= (16 if stage == 'vert' else 15), f'{name}: WebGL varying limit')
	attributes = reflection.get('inputs', []) if stage == 'vert' else []
	require(all(v['location'] < 16 for v in attributes), f'{name}: WebGL attribute limit')
	uniforms = reflection.get('ubos', [])
	require(len(uniforms) <= 12, f'{name}: WebGL uniform block limit')
	largest = max((block['block_size'] for block in uniforms), default=0)
	require(largest <= 16384, f'{name}: WebGL uniform block size limit')
	require(len(reflection.get('textures', [])) <= 16, f'{name}: WebGL sampler limit')

	return dict(attributes=len(attributes), varying_vectors=vectors,
		uniform_blocks=len(uniforms), largest_uniform_block=largest)


def stage_link(vertex, fragment):
	outputs = {v['location']: v['type'] for v in vertex['outputs']}
	inputs = {v['location']: v['type'] for v in fragment['inputs']}
	require(outputs == inputs, 'Reflected vertex/fragment interface mismatch')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--output', type=Path, required=True,
		help='new directory for native exports, reflection and report')
	parser.add_argument('--spirv-cross', default='spirv-cross')
	parser.add_argument('--spirv-val', default='spirv-val')
	parser.add_argument('--glslang', default='glslangValidator')
	args = parser.parse_args()
	build, output = args.build.resolve(), args.output.resolve()
	output.mkdir(parents=True, exist_ok=False)
	native = run(build_executable(build, 'mh-shader-contract'), output)
	(output / 'native.txt').write_text(native)
	programs, blocks, inputs, buffers = native_contract(native)
	shaders = build / 'shaders'
	targets = set((shaders / 'targets.txt').read_text().splitlines())
	require({target for _, target in programs} == targets.intersection({'vulkan', 'metal'}),
		'Embedded formats do not match the selected SDL shader outputs')
	reflections, report = {}, {}

	for (name, target), metadata in programs.items():
		extension = 'spv' if target == 'vulkan' else 'metal'
		artifact = shaders / target / f'{name}.{extension}'
		data = artifact.read_bytes()
		embedded = (output / f'{target}_{name}.bin').read_bytes()
		require(embedded == data + (b'\0' if target == 'metal' else b''),
			f'{target}/{name}: embedded bytes differ from generated artifact')
		report[f'{target}/{name}'] = dict(metadata, sha256=hashlib.sha256(data).hexdigest())

		if target == 'metal':
			check_metal(name, data.decode(), metadata)
			require(metadata['stage'] == programs[name, 'vulkan']['stage'] and
				metadata['uniforms'] == programs[name, 'vulkan']['uniforms'] and
				metadata['samplers'] == programs[name, 'vulkan']['samplers'],
				f'{name}: resource metadata changes with format')
			continue

		run(args.spirv_val, '--target-env', 'vulkan1.0', artifact)
		text = run(args.spirv_cross, artifact, '--reflect')
		(output / f'{name}.json').write_text(text)
		reflection = json.loads(text)
		check_reflection(name, metadata, reflection, blocks)
		report[f'{target}/{name}']['webgl_budget'] = check_webgl_limits(
			name, reflection, metadata['stage'])

		if metadata['stage'] == 'vert':
			check_inputs(name, reflection, inputs, buffers)

		reflections[name] = reflection
		webgl = shaders / 'webgl' / f'{name}.{metadata["stage"]}'

		if 'webgl' in targets:
			run(args.glslang, '-S', metadata['stage'], webgl)

	for vertex, fragment in PAIRS:
		stage_link(reflections[vertex + '_vert'], reflections[fragment + '_frag'])

		if 'webgl' in targets:
			run(args.glslang, '-l', shaders / 'webgl' / f'{vertex}_vert.vert',
				shaders / 'webgl' / f'{fragment}_frag.frag')

	(output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
	print(f'{len(programs)} embedded programs match generated sources and resource contracts')
	print(f'C vertex layouts, uniform offsets/strides, bindings and {len(PAIRS)} stage pairs pass')
	print('Conservative WebGL 2 attribute/varying/uniform budgets pass')

	if 'webgl' in targets:
		print('GLSL ES stages compile/link offline; run shader_webgl for browser drawing checks')

	if any(target == 'metal' for _, target in programs):
		print('Metal source bindings/entry points match; Metal compilation remains unverified')


if __name__ == '__main__':
	main()
