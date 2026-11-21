"""Held-key input for menu checks."""

import time
from tests.checks.support.x11 import xdo


def held(window, name, seconds):
	xdo('windowfocus', '--sync', window)
	xdo('keydown', name)

	try:
		time.sleep(seconds)
	finally:
		xdo('keyup', name)

	time.sleep(.15)
