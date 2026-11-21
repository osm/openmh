(async function() {
	const peer = new RTCPeerConnection();
	let timer;
	try {
		const channel = peer.createDataChannel('openmh', {
			'negotiated': true,
			'id': 0,
			'ordered': false,
			'maxRetransmits': 0
		});
		channel.binaryType = 'arraybuffer';
		const opened = new Promise((resolve, reject) => {
			channel.onopen = resolve;
			channel.onerror = () =>
				reject(new Error('Data channel failed'));
			timer = setTimeout(() => reject(new Error(
						   'Data channel timed out')),
				12000);
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
		await opened;
		clearTimeout(timer);
		for (const size of [1, 1025, 8192]) {
			const data = new Uint8Array(size);
			for (let i = 0; i < size; i++) {
				data[i] = (i * 17 + size) & 255;
			}
			await new Promise((resolve, reject) => {
				timer = setTimeout(
					() => reject(new Error(
						'Echo timed out: ' + size)),
					5000);
				channel.onmessage = event => {
					const received =
						new Uint8Array(event.data);
					if (received.length !== size ||
						received.some(
							(value, i) => value !==
								data[i])) {
						reject(new Error(
							'Echo content mismatch: ' +
							size));
					} else {
						resolve();
					}
				};
				channel.send(data);
			});
			clearTimeout(timer);
		}
		// Lose one native echo fragment, then verify that a later message
		// arrives intact and the native peer receives a FORWARD TSN ack.
		await new Promise((resolve, reject) => {
			timer = setTimeout(() => reject(new Error(
						   'Loss recovery timed out')),
				8000);
			channel.onmessage = event => {
				const received = new Uint8Array(event.data);
				if (received.length === 1 &&
					received[0] === 0xaa) {
					resolve();
				} else {
					reject(new Error(
						'Unexpected message after fragment loss'));
				}
			};
			channel.send(new Uint8Array(8192).fill(0xee));
			channel.send(new Uint8Array([0xaa]));
		});
		clearTimeout(timer);
		channel.send(new Uint8Array([0xdd]));
		response = await fetch('/input',
			{method: 'POST', body: JSON.stringify({verify: true})});
		result = await response.json();
		if (!result.ok) {
			throw new Error(result.error);
		}
		await fetch('/report',
			{method: 'POST', body: JSON.stringify({passed: true})});
	} catch (error) {
		await fetch('/report', {
			method: 'POST',
			body: JSON.stringify(
				{passed: false, error: String(error)})
		});
	} finally {
		clearTimeout(timer);
		peer.close();
	}
})();
