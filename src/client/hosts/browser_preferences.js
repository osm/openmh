'use strict';

// Transport only: descriptors, validation and persistence belong to the client.
class OpenMHPreferences {
	static textType = 3;
	static bindingFirst = 4;
	static actionCount = 18;
	static actionFirst =
		OpenMHPreferences.bindingFirst + OpenMHPreferences.actionCount;
	static feedbackAvailable =
		OpenMHPreferences.actionFirst + OpenMHPreferences.actionCount;
	static mouseAvailable = OpenMHPreferences.feedbackAvailable + 1;
	static mouseMinimum = OpenMHPreferences.mouseAvailable + 1;
	static mouseMaximum = OpenMHPreferences.mouseMinimum + 1;
	static scanCount = 256;

	constructor(player) {
		this.player = player;
	}

	call(name, result = 'number', types = [], values = []) {
		return this.player.engine.ccall(
			'mh_browser_' + name, result, types, values);
	}

	require(success) {
		if (!success)
			throw new Error(
				this.call('preferences_error', 'string') ||
				'Preference update failed.');
	}

	schema() {
		const count = this.call('preferences_count');
		if (!count)
			throw new Error('Preferences are not ready.');
		return Array.from({length: count}, (_, index) => {
			const text = (field) => this.call('preferences_text',
				'string',
				['number', 'number'],
				[index, field]);
			const number = (field) =>
				this.call('preferences_number',
					'number',
					['number', 'number'],
					[index, field]);
			const reason = text(2);
			const choices = text(1);
			return {
				key: text(0),
				type: number(0) === OpenMHPreferences.textType
					? 'string'
					: 'number',
				minimum: number(1),
				maximum: number(2),
				step: number(3),
				choices: choices
					? choices.split('|').map((choice) => {
						  const [value, label] =
							  choice.split(':');
						  return {
							  value: Number(value),
							  label
						  };
					  })
					: [],
				available: !reason,
				reason,
				applies: 'nextSession'
			};
		});
	}

	read() {
		return Object.fromEntries(this.schema().map(
			(field, index) => [field.key,
				this.call(field.type === 'string'
						? 'preferences_text'
						: 'preferences_number',
					field.type,
					['number', 'number'],
					[
						index,
						field.type === 'string' ? 3 : 4
					])]));
	}

	async change(operation) {
		const player = this.player;
		if (player.commandBusy || player.engine.saves.busy ||
			player.multiplayer.engaged ||
			!['ready', 'finished', 'failed'].includes(player.state))
			throw new Error(
				'Preferences require an idle, disconnected player.');
		player.commandBusy = true;
		try {
			await player.waitUntilReady();
			operation();
			await player.engine.saves.flush();
		} finally {
			player.commandBusy = false;
		}
	}

	object(value) {
		if (!value || typeof value !== 'object' || Array.isArray(value))
			throw new Error('Expected a preference object.');
		return Object.entries(value);
	}

	async update(values) {
		const schema = this.schema();
		const updates = this.object(values).map(([key, value]) => {
			const index =
				schema.findIndex((field) => field.key === key);
			if (index < 0)
				throw new Error('Unknown preference: ' + key);
			const field = schema[index];
			if (field.type === 'number' &&
				typeof value === 'boolean')
				value = Number(value);
			if (typeof value !== field.type ||
				(field.type === 'number'
						? !Number.isFinite(value)
						: value.includes('\0')))
				throw new Error('Invalid value for ' + key);
			return {index, value, text: field.type === 'string'};
		});
		if (!updates.length)
			return this.read();
		await this.change(() => {
			this.require(this.call('preferences_begin',
				'number',
				['number'],
				[updates.length]));
			for (const {index, value, text} of updates)
				this.require(this.call('preferences_set',
					'number',
					['number', 'number', 'string'],
					[
						index,
						text ? 0 : value,
						text ? value : ''
					]));
			this.require(this.call('preferences_commit'));
		});
		return this.read();
	}

	keys() {
		const result = [];
		for (let code = 0; code < OpenMHPreferences.scanCount; code++) {
			const name = this.call(
				'controls_key', 'string', ['number'], [code]);
			if (name)
				result.push({code, name});
		}
		return result;
	}

	control(id) {
		this.player.validateControlProfile(id);
		const get = (field) => this.call('controls_get',
			'number',
			['number', 'number'],
			[id, field]);
		return {
			id,
			mouseSpeed: [1, 2, 3].map(get),
			forceFeedback: !!get(0),
			bindings: this.player.getControlBindings(id),
			capabilities: {
				forceFeedback: !!get(
					OpenMHPreferences.feedbackAvailable),
				mouseSpeed: {
					available: !!get(OpenMHPreferences
							.mouseAvailable),
					minimum: get(
						OpenMHPreferences.mouseMinimum),
					maximum: get(
						OpenMHPreferences.mouseMaximum)
				},
				actions: Object.fromEntries(
					OpenMH.controlActions.map(
						(name, index) => [name,
							!!get(OpenMHPreferences
									.actionFirst +
								index)])),
				keys: this.keys()
			}
		};
	}

	controlUpdates(values) {
		const updates = [];
		for (const [key, value] of this.object(values)) {
			if (key === 'mouseSpeed') {
				if (!Array.isArray(value) ||
					value.length !== 3 ||
					value.some((speed) => typeof speed !==
								'number' ||
							!Number.isFinite(
								speed)))
					throw new Error(
						'Expected three mouse speeds.');
				value.forEach((speed, index) => updates.push(
						      [index + 1, speed]));
			} else if (key === 'forceFeedback') {
				if (typeof value !== 'boolean')
					throw new Error(
						'Force feedback must be a boolean.');
				updates.push([0, Number(value)]);
			} else if (key === 'bindings') {
				const keys = this.keys();
				for (const [action, name] of this.object(
					     value)) {
					const index =
						OpenMH.controlActions.indexOf(
							action);
					const binding = keys.find(
						(key) => key.name === name);
					if (index < 0 || !binding)
						throw new Error(
							'Unknown action or unavailable control binding.');
					updates.push([
						OpenMHPreferences.bindingFirst +
							index,
						binding.code
					]);
				}
			} else {
				throw new Error(
					'Unknown control preference: ' + key);
			}
		}
		return updates;
	}

	async updateControl(id, values) {
		this.player.validateControlProfile(id);
		const updates = this.controlUpdates(values);
		await this.change(() => {
			this.require(this.call(
				'controls_begin', 'number', ['number'], [id]));
			for (const [field, value] of updates)
				this.require(this.call('controls_set',
					'number',
					['number', 'number'],
					[field, value]));
			this.require(this.call('controls_commit'));
		});
		return this.control(id);
	}
}
