"""Own the lifetime of a live client and its diagnostic log."""

from contextlib import contextmanager
import subprocess
import time
from tests.checks.support.menu import wait, window_for, diagnostic
from tests.checks.support.x11 import close_window, xdo
from tests.checks.support.process import stop


@contextmanager
def frontend(command, arguments, log, env, title='main-menu preview', ready=None):
	if '--debug-window-titles' not in command + arguments:
		arguments = arguments + ['--debug-window-titles', '1']

	with log.open('w') as stream:
		process = subprocess.Popen(command + arguments, stdout=stream, stderr=stream, env=env)

		try:
			if ready is not None:
				wait(process, log, lambda: ready in log.read_text())

			menu = window_for(process, log, title)

			# Focus resets the game's first four keyboard polls.
			xdo('windowfocus', '--sync', menu)
			time.sleep(.5)
			yield process, menu
			close_window(menu)
			assert process.wait(timeout=30) == 0, diagnostic(log)
		except Exception:
			print(diagnostic(log), flush=True)

			raise
		finally:
			stop(process)
