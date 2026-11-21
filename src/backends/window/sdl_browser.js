addToLibrary({
	mh_sdl_browser_canvas__deps: ['$stringToUTF8'],
	mh_sdl_browser_canvas: function(output, capacity) {
		const canvas =
			Module['canvas'] || document.getElementById('canvas');
		if (!canvas || canvas.tagName !== 'CANVAS' ||
			!canvas.isConnected)
			return 0;

		// SDL also uses querySelector directly, so use a real DOM selector.
		// Keep the page's ID intact and avoid selecting another instance's canvas.
		let number = 0;
		let selector;
		while (true) {
			selector = 'canvas[data-openmotor-canvas="' + number +
				'"]';
			const existing = document.querySelector(selector);
			if (!existing || existing === canvas)
				break;
			number++;
		}
		if (selector.length + 1 > capacity)
			return 0;
		canvas.setAttribute('data-openmotor-canvas', String(number));
		stringToUTF8(selector, output, capacity);
		return 1;
	}
});
