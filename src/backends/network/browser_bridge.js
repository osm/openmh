addToLibrary({
	$mhBrowserNetwork: {
		// State codes match browser_internal.h.
		IDLE: 0,
		CONNECTING: 1,
		CONNECTED: 2,
		CLOSED: 3,
		MAX_HANDLES: 16,
		MAX_HANDLE_ID: 2147483647,
		MAX_DATAGRAM_BYTES: 8192,
		MAX_RECEIVE_MESSAGES: 64,
		MAX_BUFFERED_BYTES: 262144,
		MAX_SDP_BYTES: 16384,
		CONNECT_TIMEOUT_MS: 15000,
		DISCONNECT_TIMEOUT_MS: 30000,

		nextHandle: 0,
		handles: new Map(),

		stop: function(state, reason) {
			if (state.phase === mhBrowserNetwork.CLOSED) {
				return;
			}
			state.phase = mhBrowserNetwork.CLOSED;
			state.reason = reason;
			state.messages.length = 0;
			clearTimeout(state.deadline);
			clearTimeout(state.disconnected);
			if (state.abort) {
				state.abort.abort();
			}
			if (state.channel) {
				state.channel.onopen = state.channel.onmessage =
					state.channel.onerror =
						state.channel.onclose = null;
			}
			if (state.peer) {
				state.peer.onconnectionstatechange = null;
				state.peer.close();
			}
		},

		answer: async function(response) {
			if (response.status !== 200 ||
				(response.headers.get('Content-Type') || '')
						.split(';')[0]
						.trim()
						.toLowerCase() !==
					'application/sdp' ||
				!response.body) {
				throw new Error('Signaling response rejected');
			}
			const reader = response.body.getReader();
			const bytes =
				new Uint8Array(mhBrowserNetwork.MAX_SDP_BYTES);
			let size = 0;
			try {
				while (true) {
					const part = await reader.read();
					if (part.done) {
						break;
					}
					if (part.value.length >
						bytes.length - size) {
						throw new Error(
							'Signaling answer too large');
					}
					bytes.set(part.value, size);
					size += part.value.length;
				}
			} finally {
				await reader.cancel();
			}
			return new TextDecoder('utf-8', {
				fatal: true
			}).decode(bytes.subarray(0, size));
		},

		connect: async function(state, endpoint, relayOnly) {
			const net = mhBrowserNetwork;
			state.phase = net.CONNECTING;
			state.deadline = setTimeout(
				() => net.stop(
					state, 'Browser connection timed out'),
				net.CONNECT_TIMEOUT_MS);
			try {
				const url = new URL(endpoint, location.href);
				if (!['https:', 'http:'].includes(
					    url.protocol) ||
					url.username || url.password ||
					url.hash) {
					throw new Error(
						'Invalid signaling URL');
				}
				const peer = new RTCPeerConnection({
					'iceServers': state.ice,
					'iceTransportPolicy':
						relayOnly ? 'relay' : 'all'
				});
				state.peer = peer;
				const channel =
					peer.createDataChannel('openmh', {
						'negotiated': true,
						'id': 0,
						'ordered': false,
						'maxRetransmits': 0
					});
				state.channel = channel;
				channel.binaryType = 'arraybuffer';
				channel.onopen = () => {
					if (state.phase === net.CLOSED) {
						return;
					}
					if (peer.sctp.maxMessageSize > 0 &&
						peer.sctp.maxMessageSize <
							net.MAX_DATAGRAM_BYTES) {
						net.stop(state,
							'Server data channel message limit is too small');
						return;
					}
					clearTimeout(state.deadline);
					state.phase = net.CONNECTED;
				};
				channel.onmessage = event => {
					if (state.phase === net.CLOSED) {
						return;
					}
					if (!(event.data instanceof
						    ArrayBuffer) ||
						event.data.byteLength === 0 ||
						event.data.byteLength >
							net.MAX_DATAGRAM_BYTES) {
						net.stop(state,
							'Invalid server data channel message');
						return;
					}
					// Unreliable overflow drops a whole datagram; game reliable
					// delivery retries because the dropped message is not ACKed.
					if (state.messages.length <
						net.MAX_RECEIVE_MESSAGES) {
						state.messages.push(
							new Uint8Array(
								event.data));
					}
				};
				channel.onclose = () => net.stop(
					state, 'Server data channel closed');
				channel.onerror = () => net.stop(
					state, 'Server data channel failed');
				peer.onconnectionstatechange = () => {
					if (state.phase === net.CLOSED) {
						return;
					}
					clearTimeout(state.disconnected);
					if (['failed', 'closed'].includes(
						    peer.connectionState)) {
						net.stop(state,
							'Browser connection failed');
					} else if (peer.connectionState ===
						'disconnected') {
						state.disconnected = setTimeout(
							() => net.stop(state,
								'Browser connection lost'),
							net.DISCONNECT_TIMEOUT_MS);
					}
				};
				const offer = await peer.createOffer();
				if (state.phase === net.CLOSED) {
					return;
				}
				if (new TextEncoder().encode(offer.sdp).length >
					net.MAX_SDP_BYTES) {
					throw new Error(
						'Signaling offer too large');
				}
				await peer.setLocalDescription(offer);
				if (state.phase === net.CLOSED) {
					return;
				}
				// ICE-lite learns the browser endpoint from authenticated checks;
				// no trickle candidates or gathering wait are needed in signaling.
				state.abort = new AbortController();
				const response = await fetch(url.href, {
					method: 'POST',
					headers: {
						'Content-Type':
							'application/sdp'
					},
					body: offer.sdp,
					credentials: 'omit',
					cache: 'no-store',
					redirect: 'error',
					signal: state.abort.signal
				});
				if (state.phase === net.CLOSED) {
					if (response.body) {
						await response.body.cancel();
					}
					return;
				}
				const answer = await net.answer(response);
				if (state.phase !== net.CLOSED) {
					await peer.setRemoteDescription({
						'type': 'answer',
						'sdp': answer
					});
				}
			} catch (error) {
				// Keep untrusted response text, URLs and TURN credentials out of
				// diagnostics. A canceled generation cannot update its successor.
				if (state.phase !== net.CLOSED) {
					net.stop(state,
						'Cannot establish browser connection; check the signaling URL and server availability');
				}
			}
		}
	},

	mh_browser_net_open__deps: ['$mhBrowserNetwork'],
	mh_browser_net_open: function() {
		const net = mhBrowserNetwork;
		if (net.handles.size >= net.MAX_HANDLES ||
			net.nextHandle === net.MAX_HANDLE_ID) {
			return 0;
		}
		const handle = ++net.nextHandle;
		net.handles.set(handle,
			{phase: net.IDLE, ice: [], messages: [], reason: ''});
		return handle;
	},

	mh_browser_net_close__deps: ['$mhBrowserNetwork'],
	mh_browser_net_close: function(handle) {
		const net = mhBrowserNetwork;
		const state = net.handles.get(handle);
		if (state) {
			net.stop(state, 'Browser connection closed');
			net.handles.delete(handle);
		}
	},

	mh_browser_net_ice__deps: ['$mhBrowserNetwork', '$UTF8ToString'],
	mh_browser_net_ice: function(handle, url, username, credential) {
		mhBrowserNetwork.handles.get(handle).ice.push({
			'urls': UTF8ToString(url),
			'username': UTF8ToString(username),
			'credential': UTF8ToString(credential)
		});
	},

	mh_browser_net_connect__deps: ['$mhBrowserNetwork', '$UTF8ToString'],
	mh_browser_net_connect: function(handle, url, relayOnly) {
		mhBrowserNetwork.connect(mhBrowserNetwork.handles.get(handle),
			UTF8ToString(url),
			!!relayOnly);
	},

	mh_browser_net_state__deps: ['$mhBrowserNetwork'],
	mh_browser_net_state: function(handle) {
		return mhBrowserNetwork.handles.get(handle).phase;
	},

	mh_browser_net_send__deps: ['$mhBrowserNetwork'],
	mh_browser_net_send: function(handle, data, size) {
		const net = mhBrowserNetwork;
		const state = net.handles.get(handle);
		if (state.phase !== net.CONNECTED ||
			state.channel.readyState !== 'open') {
			return -1;
		}
		if (size === 0 || size > net.MAX_DATAGRAM_BYTES ||
			state.channel.bufferedAmount >
				net.MAX_BUFFERED_BYTES - size) {
			return 0;
		}
		try {
			state.channel.send(HEAPU8.subarray(data, data + size));
			return 1;
		} catch (error) {
			if (error.name === 'OperationError') {
				return 0;
			}
			net.stop(state,
				'Cannot send browser data channel message');
			return -1;
		}
	},

	mh_browser_net_receive__deps: ['$mhBrowserNetwork'],
	mh_browser_net_receive: function(handle, data, capacity) {
		const message =
			mhBrowserNetwork.handles.get(handle).messages.shift();
		if (!message || message.length > capacity) {
			return 0;
		}
		HEAPU8.set(message, data);
		return message.length;
	},

	mh_browser_net_reason__deps:
		['$mhBrowserNetwork', '$stringToUTF8', '$lengthBytesUTF8'],
	mh_browser_net_reason: function(handle, data, capacity) {
		const reason = mhBrowserNetwork.handles.get(handle).reason;
		const size = lengthBytesUTF8(reason) + 1;
		if (size > capacity) {
			return 0;
		}
		stringToUTF8(reason, data, capacity);
		return size;
	},

	mh_browser_net_disconnect__deps: ['$mhBrowserNetwork'],
	mh_browser_net_disconnect: function(handle) {
		mhBrowserNetwork.stop(mhBrowserNetwork.handles.get(handle),
			'Browser connection closed');
	},

	mh_browser_net_now: function() {
		return performance.now();
	},

	mh_browser_net_ticks: function() {
		return performance.now() >>> 0;
	}
});
