'use strict';

// Keep history in the tracked save filesystem so backups include it too.
class OpenMHRaceHistory {
	static path = '/userdata/racehistory.json';
	static limit = 100;
	static maximumBytes = 1024 * 1024;

	constructor(fs) {
		this.fs = fs;
	}

	read() {
		if (!this.fs.lookupPath(OpenMHRaceHistory.path, {
				    follow: true,
				    noent_okay: true
			    }).node)
			return [];

		if (this.fs.stat(OpenMHRaceHistory.path).size >
			OpenMHRaceHistory.maximumBytes)
			throw new Error('Race history is too large.');

		const data = JSON.parse(this.fs.readFile(
			OpenMHRaceHistory.path, {encoding: 'utf8'}));
		if (data?.version !== 1 || !Array.isArray(data.races) ||
			data.races.length > OpenMHRaceHistory.limit ||
			!data.races.every(
				(race) => OpenMHRaceHistory.validRace(race)))
			throw new Error('Race history cannot be read.');

		return data.races;
	}

	append(race) {
		if (!OpenMHRaceHistory.validRace(race))
			throw new Error('Invalid race history entry.');

		// Read before replacing: a malformed imported history must not be erased.
		const races = [race, ...this.read()].slice(
			0, OpenMHRaceHistory.limit);
		const bytes = new TextEncoder().encode(
			JSON.stringify({version: 1, races}));
		if (bytes.length > OpenMHRaceHistory.maximumBytes)
			throw new Error('Race history is too large.');

		this.fs.writeFile(OpenMHRaceHistory.path, bytes);
		return races;
	}

	static describe(config, content, rows) {
		const name = (catalog, id) =>
			content[catalog].find((item) => item.id === id)?.name ||
			'Unknown';
		return {
			date: new Date().toISOString(),
			driver: config.player.name,
			track: name('tracks', config.track),
			car: name('cars', config.car),
			mode: name('modes', config.mode),
			laps: config.laps,
			rows: structuredClone(rows)
		};
	}

	static validRace(race) {
		return (race && typeof race.date === 'string' &&
			Number.isFinite(Date.parse(race.date)) &&
			['track', 'car', 'mode', 'driver'].every(
				(key) => typeof race[key] === 'string' &&
					race[key].length > 0 &&
					race[key].length <= 80) &&
			Number.isInteger(race.laps) && race.laps > 0 &&
			race.laps <= 99 && Array.isArray(race.rows) &&
			race.rows.length > 0 && race.rows.length <= 8 &&
			race.rows.every((row) => this.validRow(row)));
	}

	static validRow(row) {
		return (row &&
			['player', 'finished', 'estimated'].every(
				(key) => typeof row[key] === 'boolean') &&
			['position', 'milliseconds', 'bestLap'].every(
				(key) => Number.isSafeInteger(row[key]) &&
					row[key] >= 0 &&
					row[key] <= 0xffffffff) &&
			row.position <= 8);
	}
}
