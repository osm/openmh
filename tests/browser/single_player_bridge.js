addToLibrary({
	mh_browser_flow_fixture_bytes: function(bytes, size) {
		Module['leagueFixture'] = HEAPU8.slice(bytes, bytes + size);
	},
	$mhWebFlowCapture: function() {
		return new Promise(function(resolve, reject) {
			Module['canvas'].toBlob(function(blob) {
				if (!blob)
					return reject(new Error(
						'Cannot capture results board'));
				fetch('/capture.png',
					{method: 'POST', body: blob})
					.then(
						function(response) {
							if (!response.ok)
								throw new Error(
									'Cannot save results capture');
							resolve();
						},
						reject)
					.catch(reject);
			}, 'image/png');
		});
	},
	mh_browser_flow_league: function(
		stage, race, points, car, tracks, cars) {
		Module['league'] = {stage, race, points, car, tracks, cars};
	},
	mh_browser_flow_season__deps: ['$mhWebFlowCapture'],
	mh_browser_flow_season: function(active) {
		// Read before the browser clears WebGL's transient drawing buffer.
		if (active && !Module['season']?.active)
			Module['seasonCapture'] = mhWebFlowCapture();
		Module['season'] = {active};
	},
	mh_browser_flow_selection: function(
		single, mode, focus, count, selected, name) {
		Module['choices'] = {
			single,
			mode,
			focus,
			count,
			selected,
			name: UTF8ToString(name)
		};
	},
	mh_browser_flow_race_setup: function(mode, laps, remote, track) {
		Module['raceSetup'] = {mode, laps, remote, track};
		Module['lastRace'] = null;
	},
	mh_browser_flow_manual_drive: function() {
		Module['inputProbe'] = {throttle: 0, steering: 0, mouse: 0};
		return Module['flowManualDrive'] ? 1 : 0;
	},
	mh_browser_flow_input: function(throttle, steering, mouseX, mouseY) {
		const probe = Module['inputProbe'];
		probe.throttle = Math.max(probe.throttle, throttle);
		probe.steering = Math.max(probe.steering, Math.abs(steering));
		probe.mouse += Math.abs(mouseX) + Math.abs(mouseY);
	},
	mh_browser_flow_movie: function(frames, interval) {
		Module['movie'] = {frames, interval};
	},
	mh_browser_flow_suspended: function(suspended, ticks) {
		Module['suspension'] = {suspended, ticks};
	},
	mh_browser_flow_menu: function(page,
		selected,
		network,
		transition,
		commits,
		recording,
		modal) {
		Module['flow'] = {
			stage: UTF8ToString(page),
			selected,
			network,
			transition,
			commits,
			recording,
			modal
		};
	},
	mh_browser_flow_race__deps: ['$mhWebFlowCapture'],
	mh_browser_flow_race: function(stage,
		lap,
		finished,
		milliseconds,
		cars,
		complete,
		frames,
		commits,
		x,
		z,
		held,
		paused,
		position,
		musicTrack) {
		Module['flow'] = {
			stage: UTF8ToString(stage),
			lap,
			finished,
			milliseconds,
			cars,
			complete,
			frames,
			commits,
			x,
			z,
			held,
			paused,
			position,
			musicTrack
		};
		Module['lastRace'] = Module['flow'];
		if (Module['flow'].stage === 'results' &&
			!Module['flowCaptured']) {
			Module['flowCaptured'] = true;
			Module['flowCapture'] = mhWebFlowCapture();
		}
	}
});
