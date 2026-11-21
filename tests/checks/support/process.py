"""Bounded cleanup for child processes owned by acceptance checks."""

import subprocess

COMMAND_TIMEOUT = 120


def stop(process, timeout=5):
	"""Reap a child, escalating from terminate to kill only when necessary."""

	if process.poll() is not None:
		return

	process.terminate()

	try:
		process.wait(timeout=timeout)
	except subprocess.TimeoutExpired:
		process.kill()
		process.wait(timeout=timeout)
