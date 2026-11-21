addToLibrary({
	mh_webgl_presentation_lost__deps: ['$GL'],
	mh_webgl_presentation_lost: function() {
		return GL.contexts.some(
			       (context) => context?.['openmotorRecovery'] &&
				       context.GLctx.isContextLost())
			? 1
			: 0;
	},
	mh_webgl_context_watch__deps: ['$GL'],
	mh_webgl_context_watch: function(handle) {
		const context = GL.contexts[handle];
		if (!context)
			return 0;
		const canvas = context.GLctx.canvas;
		const state = {generation: 0};
		state.lost = (event) => event.preventDefault();
		state.restored = () => {
			state.generation = (state.generation + 1) & 0x7fffffff;
		};
		canvas.addEventListener('webglcontextlost', state.lost);
		canvas.addEventListener('webglcontextrestored', state.restored);
		context['openmotorRecovery'] = state;
		return 1;
	},
	mh_webgl_context_generation__deps: ['$GL'],
	mh_webgl_context_generation: function(handle) {
		return (GL.contexts[handle]
				?.['openmotorRecovery']
				?.generation ??
			-1);
	},
	mh_webgl_context_unwatch__deps: ['$GL'],
	mh_webgl_context_unwatch: function(handle) {
		const context = GL.contexts[handle];
		const state = context?.['openmotorRecovery'];
		if (!state)
			return;
		context.GLctx.canvas.removeEventListener(
			'webglcontextlost', state.lost);
		context.GLctx.canvas.removeEventListener(
			'webglcontextrestored', state.restored);
		delete context['openmotorRecovery'];
	}
});
