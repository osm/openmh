addToLibrary({
	$mhBrowserLoop: {visibility: null},
	mh_browser_loop_watch_visibility__deps: [
		'$mhBrowserLoop',
		'emscripten_set_main_loop_timing',
		'emscripten_pause_main_loop',
		'emscripten_resume_main_loop'
	],
	mh_browser_loop_watch_visibility: function(enabled, interval) {
		if (mhBrowserLoop.visibility) {
			document.removeEventListener(
				'visibilitychange', mhBrowserLoop.visibility);
			mhBrowserLoop.visibility = null;
		}
		if (!enabled)
			return;
		// Emscripten's public EM_TIMING_SETTIMEOUT / EM_TIMING_RAF modes.
		const timeoutTiming = 0, animationTiming = 1;
		mhBrowserLoop.visibility = () => {
			// RAF stops in hidden tabs; timers keep application networking alive.
			_emscripten_set_main_loop_timing(document.hidden
					? timeoutTiming
					: animationTiming,
				document.hidden ? interval : 1);
			// Replace a pending RAF without waiting for the page to become visible.
			_emscripten_pause_main_loop();
			_emscripten_resume_main_loop();
		};
		document.addEventListener(
			'visibilitychange', mhBrowserLoop.visibility);
		if (document.hidden)
			mhBrowserLoop.visibility();
	}
});
