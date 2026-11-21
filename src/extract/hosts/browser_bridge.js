addToLibrary({
	mh_browser_extract_progress: function(step, detail, completed, total) {
		Module['onProgress']?.({
			operation: UTF8ToString(step),
			detail: UTF8ToString(detail),
			completed,
			total
		});
	}
});
