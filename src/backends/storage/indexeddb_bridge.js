addToLibrary({
	mh_indexeddb_begin: function(prepare) {
		return Module['storage'].begin(!!prepare);
	},

	mh_indexeddb_poll: function() {
		return Module['storage'].status;
	},

	mh_indexeddb_cancel: function() {
		Module['storage'].cancel();
	}
});
