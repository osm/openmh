"""Independent TBL05 fixture encoding and decoding."""


def reference_key(seed, size):
	"""Build the TBL05 key using integer arithmetic."""

	previous = bytes(seed)
	state = pow(len(previous), 16, 1 << 32)

	while True:
		count = min(len(previous), size // 2)
		state = state * count % (1 << 32)
		expanded = bytearray()

		for byte in previous[:count]:
			state = ((state + byte) ** 2 + byte) % (1 << 32)
			expanded.extend((state & 255, (state >> 8) & 255))

		if count == size // 2:
			return expanded + previous[1:2]

		previous = expanded


def reference_decode(data):
	if data[:4] != b"TBL\x05":
		raise ValueError("expected shipped TBL05 wrapper")

	first = reference_key(bytes([1, 3, 250, 21, 32, 250, 42, 128, 170]), 233)
	second = reference_key(b"EQ", 227)
	decoded = bytearray()
	total = 0

	for index, byte in enumerate(data[4:]):
		value = (byte - first[index % 233] - second[index % 227] - total) % 256
		decoded.append(value)
		total += value

	total = 0

	for index in range(len(decoded) - 1, -1, -1):
		decoded[index] = (decoded[index] - total) % 256
		total += decoded[index]

	return bytes(decoded)


def reference_encode(plaintext):
	"""Wrap generated test text without importing original game data."""

	first = reference_key(bytes([1, 3, 250, 21, 32, 250, 42, 128, 170]), 233)
	second = reference_key(b"EQ", 227)
	chained = bytearray(plaintext)
	total = 0

	for index in range(len(chained) - 1, -1, -1):
		total = (total + plaintext[index]) % 256
		chained[index] = total

	encoded = bytearray(b"TBL\x05")
	total = 0

	for index, byte in enumerate(chained):
		encoded.append((byte + total + first[index % 233] + second[index % 227]) % 256)
		total = (total + byte) % 256

	return bytes(encoded)
