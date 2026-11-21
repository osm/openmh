// Exercise asynchronous ownership and bounded queues with a controlled SDK.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

(async function() {
	let library;
	let fetchReply;
	let request;
	const timers = new Map();
	let nextTimer = 0;
	const peers = [];
	class Peer {
		constructor(config) {
			this.config = config;
			this.sctp = {maxMessageSize: 8192};
			this.remoteCalls = 0;
			this.sent = [];
			peers.push(this);
		}
		createDataChannel(label, options) {
			assert.equal(options.negotiated, true);
			assert.equal(options.ordered, false);
			assert.equal(options.maxRetransmits, 0);
			assert.equal(options.id, 0);
			this.channel = {
				readyState: 'open',
				bufferedAmount: 0,
				send: bytes => this.sent.push(bytes.slice())
			};
			return this.channel;
		}
		async createOffer() {
			return {type: 'offer', sdp: 'synthetic offer'};
		}
		async setLocalDescription() {}
		async setRemoteDescription() {
			this.remoteCalls++;
		}
		close() {
			this.closed = true;
		}
	}
	const context = vm.createContext({
		Map,
		Uint8Array,
		ArrayBuffer,
		TextEncoder,
		TextDecoder,
		URL,
		AbortController,
		RTCPeerConnection: Peer,
		location: {href: 'https://game.invalid/'},
		HEAPU8: new Uint8Array(65536),
		UTF8ToString: text => text,
		setTimeout: callback => {
			timers.set(++nextTimer, callback);
			return nextTimer;
		},
		clearTimeout: id => timers.delete(id),
		fetch: (url, options) => {
			request = {url, options};
			return new Promise(resolve => {
				fetchReply = resolve;
			});
		},
		addToLibrary: value => {
			library = value;
		}
	});
	vm.runInContext(fs.readFileSync(process.argv[2], 'utf8'), context);
	const net = context.mhBrowserNetwork = library.$mhBrowserNetwork;
	function response(chunks) {
		const state = {canceled: false, reads: 0};
		const cancel = async () => {
			state.canceled = true;
		};
		return {
			state,
			status: 200,
			headers: {get: () => 'application/sdp'},
			body: {
				cancel,
				getReader: () => ({
					cancel,
					read: async () => {
						state.reads++;
						return chunks.length
							? {
								  value: chunks
									  .shift(),
								  done: false
							  }
							: {done: true};
					}
				})
			}
		};
	}
	async function start() {
		const handle = library.mh_browser_net_open();
		const state = net.handles.get(handle);
		fetchReply = null;
		const pending = net.connect(state, '/', false);
		for (let i = 0; !fetchReply && i < 10; i++) {
			await Promise.resolve();
		}
		assert.ok(fetchReply);
		assert.equal(request.options.credentials, 'omit');
		assert.equal(request.options.redirect, 'error');
		return {handle, state, pending, peer: peers.at(-1)};
	}
	// Cancellation after fetch started aborts it and ignores a late answer.
	const canceled = await start();
	const lateOpen = canceled.peer.channel.onopen;
	const lateChange = canceled.peer.onconnectionstatechange;
	library.mh_browser_net_close(canceled.handle);
	assert.equal(request.options.signal.aborted, true);
	const lateReply = response([new TextEncoder().encode('answer')]);
	fetchReply(lateReply);
	await canceled.pending;
	lateOpen();
	canceled.peer.connectionState = 'disconnected';
	lateChange();
	assert.equal(canceled.peer.remoteCalls, 0);
	assert.equal(lateReply.state.reads, 0);
	assert.equal(lateReply.state.canceled, true);
	assert.equal(timers.size, 0);
	assert.equal(net.handles.size, 0);

	const live = await start();
	fetchReply(response([new TextEncoder().encode('answer')]));
	await live.pending;
	live.peer.channel.onopen();
	assert.equal(library.mh_browser_net_state(live.handle), net.CONNECTED);
	const payload = Uint8Array.from([0, 12, 34]);
	for (let i = 0; i < net.MAX_RECEIVE_MESSAGES + 20; i++) {
		live.peer.channel.onmessage({data: payload.slice().buffer});
	}
	assert.equal(live.state.messages.length, net.MAX_RECEIVE_MESSAGES);
	assert.equal(library.mh_browser_net_receive(live.handle, 100, 8192), 3);
	assert.deepEqual(
		Array.from(context.HEAPU8.subarray(100, 103)), [0, 12, 34]);
	live.peer.channel.bufferedAmount = net.MAX_BUFFERED_BYTES - 2;
	assert.equal(library.mh_browser_net_send(live.handle, 100, 3), 0);
	assert.equal(live.peer.sent.length, 0);
	live.peer.channel.bufferedAmount = 0;
	assert.equal(library.mh_browser_net_send(live.handle, 100, 3), 1);
	context.HEAPU8[101] = 99;
	assert.equal(live.peer.sent[0][1], 12);
	live.peer.channel.onmessage(
		{data: new ArrayBuffer(net.MAX_DATAGRAM_BYTES + 1)});
	assert.equal(library.mh_browser_net_state(live.handle), net.CLOSED);
	assert.equal(live.state.messages.length, 0);
	assert.equal(live.peer.closed, true);
	library.mh_browser_net_close(live.handle);
	assert.equal(timers.size, 0);

	const oversized = response(
		[new Uint8Array(net.MAX_SDP_BYTES), new Uint8Array(1)]);
	await assert.rejects(net.answer(oversized), /too large/);
	assert.equal(oversized.state.canceled, true);
	const badEncoding = response([new Uint8Array([255])]);
	await assert.rejects(net.answer(badEncoding));
	assert.equal(badEncoding.state.canceled, true);
	const timed = await start();
	const timeout = timers.get(timed.state.deadline);
	timeout();
	assert.equal(timed.state.phase, net.CLOSED);
	assert.equal(request.options.signal.aborted, true);
	fetchReply(response([]));
	await timed.pending;
	library.mh_browser_net_close(timed.handle);
	const handles = Array.from(
		{length: net.MAX_HANDLES}, () => library.mh_browser_net_open());
	assert.ok(handles.every(handle => handle > 0));
	assert.equal(library.mh_browser_net_open(), 0);
	for (const handle of handles) {
		library.mh_browser_net_close(handle);
	}
	assert.equal(net.handles.size, 0);
	assert.equal(timers.size, 0);
})().catch(error => {
	console.error(error);
	process.exitCode = 1;
});
