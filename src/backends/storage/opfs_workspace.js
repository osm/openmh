'use strict';

// Private, disposable worker filesystem. Metadata stays in memory; file pages
// live in one OPFS file so synchronous C I/O needs no asynchronous file opens.
// It is deliberately not a persistent filesystem: only the finished package
// is exported. The job owner deletes the workspace after success or failure.
class OpenMHWorkspace {
	constructor(module, handle) {
		this.FS = module.FS;
		this.MEMFS = module.MEMFS;
		this.errno = module.ERRNO_CODES;
		this.handle = handle;
		this.pageSize = 65536;
		this.zero = new Uint8Array(this.pageSize);
		this.free = [];
		this.next = 0;
		this.failure = '';
	}

	io(operation) {
		try {
			return operation();
		} catch (error) {
			this.failure = error.name === 'QuotaExceededError'
				? 'Browser storage is full. Free some space and try again.'
				: 'Cannot access temporary browser storage: ' +
					error.message;
			throw new this.FS.ErrnoError(this.errno.EIO);
		}
	}

	writeBytes(bytes, at) {
		this.io(() => {
			if (this.handle.write(bytes, {at}) !== bytes.length)
				throw new Error('Incomplete disk write.');
		});
	}

	readBytes(bytes, at) {
		this.io(() => {
			if (this.handle.read(bytes, {at}) !== bytes.length)
				throw new Error('Incomplete disk read.');
		});
	}

	page(node, index) {
		if (node.pages.has(index))
			return node.pages.get(index);

		const page = this.free.length ? this.free.pop() : this.next++;
		this.writeBytes(this.zero, page * this.pageSize);
		node.pages.set(index, page);
		return page;
	}

	resize(node, size) {
		if (size < node.usedBytes) {
			const count = Math.ceil(size / this.pageSize);
			for (const [index, page] of node.pages) {
				if (index < count)
					continue;
				this.free.push(page);
				node.pages.delete(index);
			}
			const tail = size % this.pageSize;
			const page = node.pages.get(count - 1);
			if (tail && page !== undefined)
				this.writeBytes(this.zero.subarray(tail),
					page * this.pageSize + tail);
		}
		node.usedBytes = size;
	}

	release(node) {
		if (!node.retired || node.opens)
			return;
		this.resize(node, 0);
	}

	retire(node) {
		if (!node || !this.FS.isFile(node.mode))
			return;
		node.retired = true;
		this.release(node);
	}

	read(node, buffer, offset, length, position) {
		const size = Math.min(
			length, Math.max(0, node.usedBytes - position));
		let done = 0;
		while (done < size) {
			const at = position + done;
			const within = at % this.pageSize;
			const count =
				Math.min(size - done, this.pageSize - within);
			const target = buffer.subarray(
				offset + done, offset + done + count);
			const page =
				node.pages.get(Math.floor(at / this.pageSize));
			if (page === undefined)
				target.fill(0);
			else
				this.readBytes(
					target, page * this.pageSize + within);
			done += count;
		}
		return size;
	}

	write(node, buffer, offset, length, position) {
		let done = 0;
		while (done < length) {
			const at = position + done;
			const within = at % this.pageSize;
			const count =
				Math.min(length - done, this.pageSize - within);
			const page =
				this.page(node, Math.floor(at / this.pageSize));
			this.writeBytes(buffer.subarray(offset + done,
						offset + done + count),
				page * this.pageSize + within);
			done += count;
		}
		if (length) {
			node.usedBytes =
				Math.max(node.usedBytes, position + length);
			node.mtime = node.ctime = Date.now();
		}
		return length;
	}

	fileOperations(node) {
		node.pages = new Map();
		node.opens = 0;
		node.node_ops = {
			getattr: this.MEMFS.node_ops.getattr,
			setattr: (file, attributes) => {
				const {size, ...metadata} = attributes;
				if (size !== undefined)
					this.resize(file, size);
				this.MEMFS.node_ops.setattr(file, metadata);
			}
		};
		node.stream_ops = {
			open: (stream) => {
				stream.node.opens++;
			},
			close: (stream) => {
				stream.node.opens--;
				this.release(stream.node);
			},
			llseek: this.MEMFS.stream_ops.llseek,
			read: (stream, ...args) =>
				this.read(stream.node, ...args),
			write: (stream, ...args) =>
				this.write(stream.node, ...args),
			fsync: () => {
				this.io(() => this.handle.flush());
				return 0;
			}
		};
	}

	directoryOperations(node) {
		const original = this.MEMFS.node_ops;
		node.node_ops = {
			...node.node_ops,
			mknod: (parent, name, mode) =>
				this.createNode(parent, name, mode),
			unlink: (parent, name) => {
				this.retire(parent.contents[name]);
				original.unlink(parent, name);
			},
			rename: (file, parent, name) => {
				const replaced = parent.contents[name];
				original.rename(file, parent, name);
				if (replaced !== file)
					this.retire(replaced);
			},
			symlink: () => {
				throw new this.FS.ErrnoError(
					this.errno.ENOTSUP);
			}
		};
	}

	createNode(parent, name, mode) {
		if (!this.FS.isDir(mode) && !this.FS.isFile(mode))
			throw new this.FS.ErrnoError(this.errno.ENOTSUP);

		const node = this.MEMFS.createNode(parent, name, mode, 0);
		if (this.FS.isFile(mode))
			this.fileOperations(node);
		else
			this.directoryOperations(node);
		return node;
	}

	mount(path) {
		this.FS.mkdir(path);
		this.FS.mount(
			{mount: () => this.createNode(null, '/', 0o40700)},
			{},
			path);
	}
}
