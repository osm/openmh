#!/usr/bin/env python3
"""Check opening movies and handoff to the real X11 frontend.

Requires xvfb-run/xdotool and extracted disc movies. Dummy audio consumes the
real decoded streams. Natural playback takes approximately 92 seconds.
"""

from tests.checks.support.build import build_executable
from tests.checks.support.installation import prepare_view
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.live.check_menu_credits_live import capture
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.x11 import close_window
from tests.checks.support.settings import read_cfg


from tests.checks.support.process import COMMAND_TIMEOUT


@contextmanager

def launch(args, root, label, extra=(), movies=None):
	directory = root/label
	directory.mkdir()
	log = directory/'run.log'
	cfg = directory/'settings.cfg'
	gamedir = args.gamedir

	if movies is not None:
		gamedir = directory / 'installation'
		prepare_view(gamedir, args.gamedir, {'Movies': movies})

	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(directory / 'userdata'),'--assets',str(gamedir),
		'--renderer',args.renderer,'--debug-settings',str(cfg),'--debug-no-records',*extra, '--debug-window-titles', '1']
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

	with log.open('w') as stream:
		process = subprocess.Popen(command,stdout=stream,stderr=stream,env=env)

		try:
			yield process,log,cfg
			assert process.wait(timeout=30) == 0, diagnostic(log)

			if cfg.exists():
				saved = {'glide': 'Glide', 'software': 'Software', 'direct3d': 'Direct3D', 'powervr': 'PowerVR'}
				assert read_cfg(cfg)['Renderer'] == saved[args.renderer], diagnostic(log)
		except Exception:
			print(diagnostic(log),flush=True)

			raise
		finally:
			if process.poll() is None:
				process.terminate()

				try:
					process.wait(timeout=5)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)


def movie(process, log, index):
	wait(process,log,lambda: f'Opening movie: {index}\n' in log.read_text())

	# The next index is logged just before its player creates the window.
	time.sleep(.3)

	return window_for(process,log,'^Motorhead movie$')


def menu(process, log):
	window = window_for(process,log,'main-menu preview')
	time.sleep(.6)
	close_window(window)


def diagnostics(args, root):
	env = dict(os.environ,SDL_VIDEO_DRIVER='x11',SDL_AUDIO_DRIVER='dummy')

	for label in ('finite','capture'):
		command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'),'--assets',str(args.gamedir),
			'--renderer',args.renderer,'--debug-no-settings','--debug-no-records','--audio', '0', '--debug-window-titles', '1']

		# Capture default frame count must bypass movies as well as --debug-frames.
		command += ['--debug-frames','2'] if label == 'finite' else ['--debug-capture',str(root/'menu.bmp')]
		result = subprocess.run(command,env=env,capture_output=True,text=True,timeout=30)
		(root/f'{label}.log').write_text(result.stdout+result.stderr)
		assert result.returncode == 0 and 'Opening movie:' not in result.stdout,result
		assert 'Menu preview:' in result.stdout,result

	assert (root/'menu.bmp').stat().st_size > 1000
	print('Finite frame and capture diagnostics bypass playback without --debug-no-intro',flush=True)


def run(args, root):
	if args.without_decoder:
		with launch(args,root,'without-decoder',('--audio', '0',)) as (process,log,cfg):
			menu(process,log)

		assert cfg.exists() and 'unavailable on this platform; continuing to menu' in log.read_text()
		assert 'Opening movie:' not in log.read_text()
		print('Build without the optional decoder reaches the frontend and saves CFG',flush=True)

		return

	diagnostics(args,root)

	if args.diagnostics_only:
		return

	if not args.skip_full:
		with launch(args,root,'natural') as (process,log,cfg):
			for index in range(3):
				window = movie(process,log,index)
				time.sleep(2.5)
				capture(window,root/f'opening-{index}.png')
				assert not cfg.exists(), 'Opening must precede CFG loading/saving'

			wait(process,log,lambda: bool(subprocess.run(['xdotool','search','--all','--onlyvisible',
				'--pid',str(process.pid),'--name','main-menu preview'],capture_output=True, timeout=COMMAND_TIMEOUT).stdout),seconds=100)
			menu(process,log)

		assert log.read_text().count('Opening movie:') == 3 and cfg.exists()
		print('Natural 0/1/2 playback with audio -> usable frontend and CFG save passed',flush=True)

	for index,key in ((0,'Escape'),(1,'Return'),(2,'Escape')):
		with launch(args,root,f'skip-{index}',('--audio', '0',) if index != 1 else ()) as (process,log,cfg):
			window = movie(process,log,index)
			menu_key(window,key)
			menu(process,log)

		output = log.read_text()
		assert output.count('Opening movie:') == index+1 and cfg.exists(),output
		assert 'Menu preview: selection 0,' in output and 'Dialog:' not in output,output

	print('Enter/Escape at all three stages skips the remaining sequence without entering a menu action',flush=True)

	with launch(args,root,'close',('--audio', '0',)) as (process,log,cfg):
		window = movie(process,log,0)
		close_window(window)

	assert log.read_text().count('Opening movie:') == 1 and not cfg.exists()
	assert 'Menu preview:' not in log.read_text()
	print('Movie window close exits before creating frontend state or writing CFG',flush=True)

	empty = root/'empty-movies'
	empty.mkdir()
	broken = root/'broken-movies'
	broken.mkdir()
	archive = broken/'movies.pak'
	archive.write_bytes(b'bad archive')

	for label,extra,count,movies in (
		('bypass',('--debug-no-intro', '--debug-skip-loading-wait', '1','--audio', '0'),0,None),
		('missing',('--audio', '0',),0,empty),
		('broken',('--audio', '0',),3,broken),
		('direct-main',('--debug-page','main','--audio', '0'),0,None),
		('direct-page',('--debug-page','credits','--audio', '0'),0,None)):
		with launch(args,root,label,extra,movies) as (process,log,cfg):
			menu(process,log)

		assert log.read_text().count('Opening movie:') == count
		assert cfg.exists()

	assert archive.read_bytes() == b'bad archive'
	print('Explicit bypass, direct page, missing/corrupt archive continue safely; source bytes preserved',flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build',type=Path,default=Path('build/linux'))
	parser.add_argument('--gamedir',type=Path,required=True)
	parser.add_argument('--output',type=Path)
	parser.add_argument('--skip-full',action='store_true')
	parser.add_argument('--without-decoder',action='store_true')
	parser.add_argument('--diagnostics-only',action='store_true')
	parser.add_argument('--renderer', choices=('glide', 'software', 'direct3d', 'powervr'), default='glide')
	args = parser.parse_args()

	if args.output:
		args.output.mkdir(parents=True,exist_ok=True)
		run(args,args.output)
	else:
		with tempfile.TemporaryDirectory(prefix='mh-opening-live-') as directory:
			run(args,Path(directory))


if __name__ == '__main__':
	main()
