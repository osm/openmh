'use strict';

/* A small, versioned table followed by raw file bytes and a SHA-256 digest. */
class OpenMHBackup {
	// Existing backup files start with this signature.
	static magic = new TextEncoder().encode('OpenMotor saves\n');
	static maximumBytes = 512 * 1024 * 1024;
	static maximumTable = 4 * 1024 * 1024;

	static async encode(snapshot) {
		OpenMHStorage.validate(snapshot);
		const entries = snapshot.entries.map((entry) => entry.directory
				? {path: entry.path, directory: true}
				: {path: entry.path, size: entry.data.length});
		const table = new TextEncoder().encode(
			JSON.stringify({version: 1, entries}));
		const files =
			snapshot.entries.filter((entry) => !entry.directory)
				.map((entry) => entry.data);
		const header = new Uint8Array(this.magic.length + 4);
		header.set(this.magic);
		new DataView(header.buffer)
			.setUint32(this.magic.length, table.length, true);
		const body = new Blob([header, table, ...files]);
		if (table.length > this.maximumTable ||
			body.size + 32 > this.maximumBytes)
			throw new Error(
				'Save backup exceeds the 512 MiB limit.');
		const digest = await crypto.subtle.digest(
			'SHA-256', await body.arrayBuffer());
		return new Blob(
			[body, digest], {type: 'application/octet-stream'});
	}

	static async decode(file) {
		const body = await this.verifiedBody(file);
		const {snapshot, position} = this.decodeTable(body);

		this.decodeFiles(snapshot.entries, body, position);
		OpenMHStorage.validate(snapshot);

		return snapshot;
	}

	static async verifiedBody(file) {
		if (file.size > this.maximumBytes ||
			file.size < this.magic.length + 4 + 32)
			throw new Error('Invalid save backup size.');

		const bytes = new Uint8Array(await file.arrayBuffer());

		if (!this.magic.every((byte, i) => bytes[i] === byte))
			throw new Error('This is not an OpenMH save backup.');

		const end = bytes.length - 32;
		const body = bytes.subarray(0, end);
		const digest = new Uint8Array(
			await crypto.subtle.digest('SHA-256', body));

		if (!digest.every((byte, i) => byte === bytes[end + i]))
			throw new Error(
				'The save backup is damaged or incomplete.');

		return body;
	}

	static decodeTable(body) {
		const tableSize = new DataView(
			body.buffer).getUint32(this.magic.length, true);
		const start = this.magic.length + 4;

		if (tableSize > this.maximumTable ||
			tableSize > body.length - start)
			throw new Error('Invalid save backup table.');

		const position = start + tableSize;
		const text = new TextDecoder('utf-8', {
			fatal: true
		}).decode(body.subarray(start, position));
		const snapshot = JSON.parse(text);

		if (!snapshot || snapshot.version !== 1 ||
			!Array.isArray(snapshot.entries))
			throw new Error('Unsupported save backup version.');

		return {snapshot, position};
	}

	static decodeFiles(entries, body, position) {
		for (const entry of entries) {
			if (!entry || typeof entry !== 'object')
				throw new Error('Invalid save backup entry.');

			if (entry.directory === true)
				continue;

			if (!Number.isSafeInteger(entry.size) ||
				entry.size < 0 ||
				entry.size > body.length - position)
				throw new Error(
					'Invalid save backup file size.');

			entry.data =
				body.subarray(position, position + entry.size);
			position += entry.size;
		}

		if (position !== body.length)
			throw new Error('Unexpected bytes in save backup.');
	}
}
