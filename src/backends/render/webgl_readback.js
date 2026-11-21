addToLibrary({
	mh_webgl_download_bytes__deps: ['$GL'],
	mh_webgl_download_bytes: function(output, bytes) {
		GL.currentContext.GLctx.getBufferSubData(
			0x88eb, 0, HEAPU8, output, bytes);
	}
});
