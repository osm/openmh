addToLibrary({
	mh_browser_storage_failed: function(message) {
		if (Module['onStorageError'])
			Module['onStorageError'](Module['storage'].error ||
				UTF8ToString(message));
	},

	mh_browser_finished: function(result) {
		if (Module['onGameExit'])
			Module['onGameExit'](result);
	}
});
