(async function() {
	try {
		for (const family of [4, 6]) {
			const peer = new RTCPeerConnection();
			try {
				peer.createDataChannel('openmh', {
					'negotiated': true,
					'id': 0,
					'ordered': false,
					'maxRetransmits': 0
				});
				const offer = await peer.createOffer();
				await peer.setLocalDescription(offer);
				const response = await fetch('/input', {
					method: 'POST',
					body: JSON.stringify(
						{family, offer: offer.sdp})
				});
				const result = await response.json();
				if (!result.ok) {
					throw new Error(result.error);
				}
				const answer =
					await (await fetch('/answer')).text();
				await peer.setRemoteDescription(
					{'type': 'answer', 'sdp': answer});
				if (peer.signalingState !== 'stable') {
					throw new Error(
						'Answer did not complete negotiation');
				}
			} finally {
				peer.close();
			}
		}
		await fetch('/report',
			{method: 'POST', body: JSON.stringify({passed: true})});
	} catch (error) {
		await fetch('/report', {
			method: 'POST',
			body: JSON.stringify(
				{passed: false, error: String(error)})
		});
	}
})();
