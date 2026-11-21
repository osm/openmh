"""Check CLI choices and renderer linkage in a configured native release or test build."""

import argparse
import subprocess


# The unlit menu path and software HUD asset decoding are shared across profiles.
IMPLEMENTATIONS = {
	'software': {'mh_race_draw_software_scene', 'mh_software_backend_init'},
	'glide': {'mh_glide_texture_policy'},
	'direct3d': {'mh_direct3d_material_policy', 'mh_direct3d_texture_policy'},
	'powervr': {
		'mh_powervr_attached_halo_quads', 'mh_powervr_world_lamp_quads',
		'mh_powervr_lamp_atlas_prepare', 'mh_powervr_lamp_atlas_names',
		'mh_powervr_name_mesh_create', 'mh_powervr_texture_policy',
	},
	'openmh': {'mh_openmh_scene_lighting', 'mh_openmh_light_buffer_prepare'},
}


def help_text(binary):
	return subprocess.check_output([binary, '--help'], text=True)


def option_choices(help_output, option):
	for line in help_output.splitlines():
		if line.strip().startswith(option + ' NAME '):
			choices = line.split(' NAME ', 1)[1].split(' (default:', 1)[0]

			return set(choices.strip().rstrip('.').split(', '))

	return set()


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('client')
	parser.add_argument('nm')
	parser.add_argument('renderers')
	parser.add_argument('--protocols', default='')
	parser.add_argument('--server')
	parser.add_argument('--legacy-server', action='store_true')
	parser.add_argument('--webrtc', action='store_true')
	args = parser.parse_args()
	renderers = set(args.renderers.split(';'))
	protocols = set(args.protocols.split(';')) if args.protocols else set()
	usage = help_text(args.client)
	assert option_choices(usage, '--renderer') == renderers, usage
	assert option_choices(usage, '--network-protocol') == protocols, usage
	assert ('--connect ' in usage) == bool(protocols), usage

	symbols = {line.split()[-1] for line in subprocess.check_output(
		[args.nm, '-g', args.client], text=True).splitlines() if line.split()}

	for profile in IMPLEMENTATIONS.keys() - renderers:
		leaks = symbols & IMPLEMENTATIONS[profile]
		assert not leaks, f'{profile} is disabled but the client links {sorted(leaks)}'

	if args.server:
		usage = help_text(args.server)
		assert ('--web-address ' in usage) == args.webrtc, usage
		default = 'Legacy protocol' if args.legacy_server else 'OpenMH protocol'
		assert default in usage, usage


if __name__ == '__main__':
	main()
