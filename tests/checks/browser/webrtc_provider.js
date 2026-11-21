(async function() {
	const peers = [];
	const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
	async function waitFor(test, label) {
		const deadline = performance.now() + 10000;
		while (!test()) {
			if (performance.now() >= deadline) {
				throw new Error('Timed out: ' + label);
			}
			await delay(10);
		}
	}
	function createPeer() {
		const peer = new RTCPeerConnection();
		const channel = peer.createDataChannel('openmh', {
			'negotiated': true,
			'id': 0,
			'ordered': false,
			'maxRetransmits': 0
		});
		channel.binaryType = 'arraybuffer';
		const state = {
			peer,
			channel,
			messages: [],
			acks: new Set(),
			lastSequence: 0,
			duplicates: 0
		};
		channel.onmessage = event => {
			const data = new Uint8Array(event.data);
			if (data.length === 1) {
				state.acks.add(data[0]);
				return;
			}
			const sequence = data[0];
			const duplicate = sequence !== 0 &&
				sequence === state.lastSequence;
			// Drop the first ACK for native reliable sequence 2. A duplicate
			// must be retransmitted by the game delivery layer and suppressed.
			if (sequence !== 0 && (sequence !== 2 || duplicate)) {
				channel.send(new Uint8Array([sequence]));
			}
			if (duplicate) {
				state.duplicates++;
			} else {
				state.messages.push({
					reliable: sequence !== 0,
					data: data.slice(1)
				});
				if (sequence !== 0) {
					state.lastSequence = sequence;
				}
			}
		};
		return state;
	}
	async function expect(state, payload, reliable) {
		await waitFor(() => state.messages.length !== 0, 'echo');
		const message = state.messages.shift();
		if (message.reliable !== reliable ||
			message.data.length !== payload.length ||
			message.data.some((value, i) => value !== payload[i])) {
			throw new Error('Provider message mismatch');
		}
	}
	async function exercise(state, index) {
		await waitFor(() => state.channel.readyState === 'open',
			'channel open');
		for (const size of [1, 8191]) {
			const payload = Uint8Array.from({length: size},
				(_, i) => (i * 17 + index) & 255);
			const datagram = new Uint8Array(size + 1);
			datagram.set(payload, 1);
			state.channel.send(datagram);
			await expect(state, payload, false);
		}
		const reliable = new Uint8Array([2, index, 0x52]);
		state.channel.send(reliable);
		state.channel.send(reliable);
		await expect(state, reliable.slice(1), true);
		await waitFor(() => state.acks.has(2) && state.duplicates !== 0,
			'reliable retry');
		state.channel.send(new Uint8Array([3, index, 0x53]));
		await expect(state, new Uint8Array([index, 0x53]), true);
		await waitFor(() => state.acks.has(3), 'second reliable ACK');
	}
	try {
		const offers = [];
		for (let i = 0; i < 2; i++) {
			const state = createPeer();
			peers.push(state);
			const offer = await state.peer.createOffer();
			await state.peer.setLocalDescription(offer);
			offers.push(offer.sdp);
		}
		let response = await fetch('/input',
			{method: 'POST', body: JSON.stringify({offers})});
		let result = await response.json();
		if (!result.ok) {
			throw new Error(result.error);
		}
		for (let i = 0; i < peers.length; i++) {
			const answer =
				await (await fetch('/answer' + i)).text();
			await peers[i].peer.setRemoteDescription(
				{'type': 'answer', 'sdp': answer});
		}
		await Promise.all(peers.map(exercise));
		const idle = Number(await (await fetch('/idle')).text());
		await delay(idle);
		for (let i = 0; i < peers.length; i++) {
			peers[i].channel.send(new Uint8Array([0, i, 0xdd]));
		}
		response = await fetch('/input',
			{method: 'POST', body: JSON.stringify({verify: true})});
		result = await response.json();
		if (!result.ok) {
			throw new Error(result.error);
		}
		await fetch('/report', {
			method: 'POST',
			body: JSON.stringify({passed: true, idle})
		});
	} catch (error) {
		await fetch('/report', {
			method: 'POST',
			body: JSON.stringify(
				{passed: false, error: String(error)})
		});
	} finally {
		for (const state of peers) {
			state.peer.close();
		}
	}
})();
