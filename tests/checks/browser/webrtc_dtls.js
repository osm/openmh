(async function() {
	const peer = new RTCPeerConnection();
	const candidates = [];
	peer.onicecandidate = event => {
		if (event.candidate) {
			candidates.push(event.candidate.candidate);
		}
	};
	try {
		peer.createDataChannel('openmh', {
			'negotiated': true,
			'id': 0,
			'ordered': false,
			'maxRetransmits': 0
		});
		const offer = await peer.createOffer();
		await peer.setLocalDescription(offer);
		let response = await fetch('/input', {
			method: 'POST',
			body: JSON.stringify({offer: offer.sdp})
		});
		let result = await response.json();
		if (!result.ok) {
			throw new Error(result.error);
		}
		const answer = await (await fetch('/answer')).text();
		await peer.setRemoteDescription(
			{'type': 'answer', 'sdp': answer});
		await new Promise((resolve, reject) => {
			const deadline = performance.now() + 15000;
			function check() {
				const state = peer.sctp?.transport.state;
				if (state === 'connected') {
					resolve();
				} else if (state === 'failed' ||
					performance.now() > deadline) {
					reject(new Error(
						'DTLS did not connect: ' +
						state + ', ICE: ' +
						peer.iceConnectionState));
				} else {
					setTimeout(check, 20);
				}
			}
			check();
		});
		response = await fetch('/input',
			{method: 'POST', body: JSON.stringify({verify: true})});
		result = await response.json();
		if (!result.ok) {
			throw new Error(result.error);
		}
		await fetch('/report',
			{method: 'POST', body: JSON.stringify({passed: true})});
	} catch (error) {
		const stats = Array.from((await peer.getStats()).values());
		await fetch('/report', {
			method: 'POST',
			body: JSON.stringify({
				passed: false,
				error: String(error),
				candidates,
				stats
			})
		});
	} finally {
		peer.close();
	}
})();
