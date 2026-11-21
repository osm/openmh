addToLibrary({
	$mh_openal_activation: new WeakMap(),
	mh_openal_resume__deps: ['$AL', '$mh_openal_activation'],
	mh_openal_resume: function() {
		const context = AL.currentCtx.audioCtx;
		if (context.state !== 'suspended' ||
			!navigator.userActivation?.hasBeenActive)
			return;

		// The SDK's once-only key listener can fire on Escape, which does not
		// activate audio in Firefox. Retry after an accepted user interaction,
		// as SDL does, without accumulating a promise on every output update.
		const now = performance.now();
		if (now < (mh_openal_activation.get(context) || 0))
			return;
		mh_openal_activation.set(context, now + 250);
		context.resume().catch(() => {});
	}
});
